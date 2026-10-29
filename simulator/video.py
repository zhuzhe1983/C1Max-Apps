"""本机 FFmpeg 加速服务；原始 MIPS 应用继续负责 FIFO、控件和 framebuffer。"""
import fractions
import json
import math
import pathlib
import secrets
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request


def avi_packets(stream):
    """解析我们自己生成的流式 AVI，取出交错的 YUV420 和 S16LE PCM。"""
    def read(size):
        data = bytearray()
        while len(data) < size:
            part = stream.read(size - len(data))
            if not part: raise EOFError()
            data.extend(part)
        return bytes(data)
    header = read(12)
    if header[:4] != b'RIFF' or header[8:] != b'AVI ': raise ValueError('invalid_avi')
    while True:
        try: header = read(8)
        except EOFError: return
        kind, size = header[:4], struct.unpack('<I', header[4:])[0]
        if kind in (b'LIST', b'RIFF'):
            if size < 4: raise ValueError('invalid_avi')
            group = read(4)
            if group in (b'movi', b'rec ', b'AVIX'): continue
            size -= 4
        if size > 4 * 1024 * 1024: raise ValueError('oversized_avi_packet')
        data = read(size)
        if size & 1: read(1)
        if kind in (b'00dc', b'00db'): yield 'V', data
        elif kind == b'01wb': yield 'A', data


class VideoService:
    def __init__(self, token, port, proxy=None):
        self.token, self.port, self.proxy = token, port, proxy
        self.ffmpeg, self.ffprobe = shutil.which('ffmpeg'), shutil.which('ffprobe')
        self.lock = threading.Lock(); self.sessions = {}; self.closed = False

    def serve(self, handler):
        session = None
        try:
            source = handler.headers.get('X-C1Max-URL', '')
            length = int(handler.headers.get('Content-Length', '0'))
            if not 0 <= length <= 64 * 1024 * 1024 or any(ord(c) < 32 for c in source): raise ValueError('invalid_source')
            if not self.ffmpeg or not self.ffprobe: raise ValueError('ffmpeg_unavailable')
            session = Session(self, handler, source, handler.headers.get('X-C1Max-Silent') == '1')
            address = urllib.parse.urlsplit(source)
            if address.scheme in ('http', 'https'):
                if length or not address.hostname or address.username or address.password: raise ValueError('invalid_source')
                if address.hostname == '10.0.2.2':
                    source = urllib.parse.urlunsplit(address._replace(netloc='127.0.0.1' + (':'+str(address.port) if address.port else '')))
                session.source = source
            elif source.startswith('/') and length:
                file = tempfile.NamedTemporaryFile(prefix='c1max-video-', suffix='.mp4', delete=False)
                session.upload = pathlib.Path(file.name)
                with file:
                    remaining = length
                    while remaining:
                        block = handler.rfile.read(min(65536, remaining))
                        if not block: raise ValueError('incomplete_upload')
                        file.write(block); remaining -= len(block)
                session.source = file.name
            else: raise ValueError('invalid_source')
            with self.lock:
                if self.closed or len(self.sessions) >= 4: raise ValueError('video_busy')
                self.sessions[session.ident] = session
            handler.send_response(101); handler.send_header('Connection', 'Upgrade')
            handler.send_header('Upgrade', 'c1max-video-v1'); handler.end_headers(); handler.wfile.flush()
            session.run()
        except (OSError, ValueError, EOFError, KeyError, StopIteration, ZeroDivisionError, subprocess.SubprocessError):
            if session and session.started:
                try: session.send('X decode_failed\n')
                except OSError: pass
            else: handler.reply(400, {'error': '视频加速服务无法打开输入'})
        finally:
            handler.close_connection = True
            if session:
                session.cancel()
                with self.lock: self.sessions.pop(session.ident, None)
                if session.upload: session.upload.unlink(missing_ok=True)

    def source(self, handler, ident):
        if handler.headers.get('X-C1Sim-Token') != self.token:
            handler.reply(403, {'error': '会话令牌无效'}); return
        with self.lock: session = self.sessions.get(ident)
        if not session or not session.alive:
            handler.reply(404, {'error': '播放会话已结束'}); return
        if session.upload:
            handler.reply(400, {'error': '上传文件不使用网络转发'}); return
        headers = dict(session.headers)
        if handler.headers.get('Range'): headers['Range'] = handler.headers['Range']
        host = urllib.parse.urlsplit(session.source).hostname
        proxies = {'http': self.proxy, 'https': self.proxy} if self.proxy and host not in ('127.0.0.1', 'localhost') else {}
        opener = urllib.request.build_opener(urllib.request.ProxyHandler(proxies))
        try:
            try: response = opener.open(urllib.request.Request(session.source, headers=headers), timeout=15)
            except urllib.error.HTTPError as error: response = error
            with response:
                handler.send_response(response.code)
                for name in ('Content-Length', 'Content-Type', 'Content-Range', 'Accept-Ranges'):
                    if response.headers.get(name): handler.send_header(name, response.headers[name])
                handler.send_header('Connection', 'close'); handler.end_headers()
                while session.alive:
                    block = response.read(65536)
                    if not block: break
                    handler.wfile.write(block)
        except OSError:
            # 上游地址含签名，错误日志不输出地址或原始异常文本。
            pass
        handler.close_connection = True

    def close(self):
        with self.lock: self.closed = True; sessions = list(self.sessions.values())
        for session in sessions: session.cancel()


class Session:
    def __init__(self, service, handler, source, silent):
        self.service, self.handler, self.source, self.silent = service, handler, source, silent
        self.ident = secrets.token_hex(16); self.upload = None; self.started = False
        self.headers = {k: handler.headers[k] for k in ('User-Agent', 'Referer') if handler.headers.get(k)}
        self.condition = threading.Condition(); self.output = threading.Lock()
        self.alive = True; self.paused = False; self.preview = False; self.generation = 0
        self.target = 0.; self.epoch = None; self.duration = 0.; self.process = None

    def send(self, line, data=b'', generation=None):
        with self.output:
            with self.condition:
                if not self.alive or generation is not None and generation != self.generation: return
            self.handler.wfile.write(line.encode('ascii') + data); self.handler.wfile.flush()

    def position(self):
        return min(self.duration, max(0, time.monotonic() - self.epoch if self.epoch is not None and not self.paused else self.target))

    def cancel(self):
        with self.condition:
            self.alive = False; process = self.process; self.condition.notify_all()
        if process and process.poll() is None:
            try: process.terminate()
            except ProcessLookupError: return
            try: process.wait(timeout=2)
            except subprocess.TimeoutExpired: process.kill(); process.wait()

    def controls(self):
        try:
            while self.alive:
                raw = self.handler.rfile.readline(2049)
                if not raw or len(raw) > 2048: break
                command = raw.decode('ascii').strip()
                for prefix in ('pausing_keep_force ', 'pausing_keep '):
                    if command.startswith(prefix): command = command[len(prefix):]; break
                if command == 'quit': break
                if command == 'get_time_pos':
                    with self.condition: position = self.position()
                    self.send(f'T {position:.6f}\n'); continue
                with self.condition:
                    position = self.position(); process = self.process
                    if command == 'pause':
                        self.target = position; self.paused = not self.paused; self.preview = False
                    elif command.startswith('seek '):
                        fields = command.split()
                        if len(fields) != 3 or fields[2] not in ('0', '2'): continue
                        value = float(fields[1])
                        if not math.isfinite(value): continue
                        self.target = max(0, min(self.duration, value + position if fields[2] == '0' else value))
                        self.preview = self.paused
                    else: continue
                    self.epoch = None; self.generation += 1
                    generation, target, paused = self.generation, self.target, self.paused
                    self.condition.notify_all()
                self.send(f'S {generation} {target:.6f} {int(paused)}\n')
                if process and process.poll() is None: process.terminate()
        except (OSError, ValueError, UnicodeError): pass
        finally: self.cancel()

    def run(self):
        self.started = True
        source = self.source if self.upload else f'http://127.0.0.1:{self.service.port}/video/source/{self.ident}'
        options = [] if self.upload else ['-rw_timeout', '15000000', '-headers', f'X-C1Sim-Token: {self.service.token}\r\n']
        probe = subprocess.Popen([self.service.ffprobe, '-v', 'error', *options, '-show_streams', '-show_format', '-of', 'json', source],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        with self.condition:
            self.process = probe
            if not self.alive: probe.terminate()
        try: output, error = probe.communicate(timeout=20)
        finally:
            if probe.poll() is None: probe.kill(); probe.wait()
        if probe.returncode:
            self.send('X http_403\n' if b'403' in error else 'X open_failed\n'); return
        info = json.loads(output); video = next(s for s in info['streams'] if s['codec_type'] == 'video')
        width, height = int(video['width']), int(video['height'])
        if video['codec_name'] != 'h264' or video.get('pix_fmt') != 'yuv420p' or video.get('field_order') not in (None, 'unknown', 'progressive') or \
                width < 2 or height < 2 or width > 960 or height > 960 or width * height > 307200 or width % 2 or height % 2:
            self.send('X unsupported_format\n'); return
        if width > 400: height = max(2, height * 400 // width & ~1); width = 400
        if height > 288: width = max(2, width * 288 // height & ~1); height = 288
        rate_text = video.get('avg_frame_rate') or '20/1'
        rate = fractions.Fraction('20/1' if rate_text == '0/0' else rate_text)
        rate = min(fractions.Fraction(20), max(fractions.Fraction(1), rate))
        aspect_text = video.get('sample_aspect_ratio') or '1:1'
        aspect = fractions.Fraction(('1:1' if aspect_text == 'N/A' else aspect_text).replace(':', '/'))
        if aspect <= 0: aspect = fractions.Fraction(1)
        self.duration = float(info['format'].get('duration', 0))
        if not math.isfinite(self.duration) or not 0 < self.duration <= 86400: raise ValueError('invalid_duration')
        audio = not self.silent and any(s['codec_type'] == 'audio' for s in info['streams'])
        self.send(f'M {self.duration:.6f} {width} {height} {rate.numerator} {rate.denominator} {aspect.numerator} {aspect.denominator} {int(audio)}\n')
        thread = threading.Thread(target=self.controls, daemon=True); thread.start()
        try:
            while self.alive:
                with self.condition:
                    while self.alive and self.paused and not self.preview: self.condition.wait(.2)
                    if not self.alive: break
                    generation, target, preview = self.generation, self.target, self.preview
                    self.preview = False
                seek = ['-ss', f'{target:.6f}'] if target > 0 else []
                command = [self.service.ffmpeg, '-nostdin', '-hide_banner', '-loglevel', 'error', *options,
                           *seek, '-noautorotate', '-i', source, '-map', '0:v:0',
                           '-vf', f'scale={width}:{height}:flags=fast_bilinear,fps={rate},format=yuv420p',
                           '-c:v', 'rawvideo', '-pix_fmt', 'yuv420p', '-vtag', 'I420']
                if audio and not preview: command += ['-map', '0:a:0', '-c:a', 'pcm_s16le', '-ar', '48000', '-ac', '2']
                else: command += ['-an']
                if preview: command += ['-frames:v', '1']
                command += ['-f', 'avi', 'pipe:1']
                process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=65536)
                with self.condition:
                    self.process = process
                    if generation != self.generation or not self.alive: process.terminate()
                frames = samples = 0
                try:
                    try:
                        for kind, data in avi_packets(process.stdout):
                            if kind == 'V':
                                # AVI 用空块填补精确跳转后的首个帧时间槽。
                                if not data: frames += 1; continue
                                if len(data) != width * height * 3 // 2: raise ValueError('invalid_frame')
                                position = target + frames / float(rate); frames += 1
                            else:
                                if len(data) % 4: raise ValueError('invalid_pcm')
                                position = target + samples / 192000.; samples += len(data)
                            with self.condition:
                                if generation != self.generation or not self.alive: break
                                if self.epoch is None and not preview: self.epoch = time.monotonic() - target
                                while not preview and self.alive and generation == self.generation and time.monotonic() < self.epoch + position:
                                    self.condition.wait(min(.05, self.epoch + position - time.monotonic()))
                            self.send(f'{kind} {position:.6f} {len(data)}\n', data, generation)
                    except EOFError:
                        # 暂停/跳转会终止旧进程，最后一个 AVI 包可能只写了一部分。
                        with self.condition:
                            if self.alive and generation == self.generation: raise
                finally:
                    # 停止读取旧流时先关闭管道，解除 FFmpeg 写满 stdout 后的阻塞。
                    process.stdout.close()
                    with self.condition: current = self.alive and generation == self.generation
                    if process.poll() is None:
                        if not current: process.terminate()
                        try: process.wait(timeout=2)
                        except subprocess.TimeoutExpired: process.kill(); process.wait()
                with self.condition: current = self.alive and generation == self.generation
                if current and not preview:
                    self.send(f'E {int(process.returncode != 0)}\n'); break
        finally: self.cancel()
