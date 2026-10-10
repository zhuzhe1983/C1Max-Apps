#!/usr/bin/env python3
"""Private C1 Max raw-frame bridge -> standard HLS. Python 3.9+, ffmpeg.

This is an optional Mac companion, not a Cast/AirPlay implementation. Kodi or
another renderer on the SAME paired Mac receives the resulting HLS stream.
No uploads, shell commands, arbitrary file paths or upstream URLs are accepted.
"""
import argparse
import contextlib
import hmac
import http.server
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import signal
import socket
import socketserver
import struct
import subprocess
import tempfile
import threading
import time

PROTOCOL = "c1max-game-v1"
WIDTH, HEIGHT, FPS = 320, 240, 20
FRAME_BYTES, AUDIO_LIMIT = WIDTH * HEIGHT * 2, 44100
CONFIG = Path.home() / "Library/Application Support/C1MaxCast/config.json"


def load_config(path):
    if path.stat().st_mode & 0o077:
        raise ValueError("Pairing config must be private (chmod 600)")
    if path.stat().st_size > 4096:
        raise ValueError("Pairing config too large")
    config = json.loads(path.read_text())
    if not re.fullmatch(r"[0-9a-f]{64}", config.get("token", "")):
        raise ValueError("Invalid token; run pair.py first")
    socket.inet_aton(config["host"])
    for key, default in (("port", 28766), ("http_port", 28767)):
        config.setdefault(key, default)
        if type(config[key]) is not int or not 1024 <= config[key] <= 65535:
            raise ValueError("Invalid bridge port")
    if config["port"] == config["http_port"]:
        raise ValueError("Control and HTTP ports must differ")
    return config


def receive(sock, count):
    result = bytearray()
    while len(result) < count:
        data = sock.recv(count - len(result))
        if not data:
            raise EOFError()
        result.extend(data)
    return bytes(result)


class Session:
    def __init__(self, bridge, app, title):
        self.bridge, self.app, self.title = bridge, app, title
        self.key = secrets.token_hex(24)
        self.directory = Path(tempfile.mkdtemp(prefix="c1max-game-"))
        self.lock = threading.Lock()
        self.close_lock = threading.Lock()
        self.disposed = False
        self.closed = threading.Event()
        self.frame = bytes(FRAME_BYTES)
        self.audio = bytearray()
        self.last_input = time.monotonic()
        self.created = self.last_input
        self.frames = self.audio_received = self.dropped_audio = 0
        self.first_video_time = self.last_video_time = None
        self.process = self.preview = None
        self.threads = []
        self.readers = []
        self.writers = []
        self.started = threading.Event()
        self.log = open(self.directory / "encoder.log", "wb")
        try:
            video_r, video_w = os.pipe()
            audio_r, audio_w = os.pipe()
            self.readers = [video_r, audio_r]
            self.writers = [video_w, audio_w]
            # Small probing limits are essential: otherwise ffmpeg waits for
            # seconds of raw audio before writing the first HLS segment.
            command = [bridge.ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "warning",
                       "-thread_queue_size", "2", "-f", "rawvideo", "-pixel_format", "rgb565le",
                       "-video_size", f"{WIDTH}x{HEIGHT}", "-framerate", str(FPS),
                       "-probesize", "32", "-analyzeduration", "0", "-i", f"pipe:{video_r}",
                       "-thread_queue_size", "8", "-f", "s16le", "-ar", "44100", "-ac", "2",
                       "-probesize", "32", "-analyzeduration", "0", "-i", f"pipe:{audio_r}",
                       "-map", "0:v:0", "-map", "1:a:0", "-c:v", "libx264",
                       "-preset", "ultrafast", "-tune", "zerolatency", "-threads", "2",
                       "-pix_fmt", "yuv420p", "-vf", "setsar=1", "-b:v", "1200k",
                       "-g", str(FPS), "-keyint_min", str(FPS), "-sc_threshold", "0",
                       "-c:a", "aac", "-b:a", "96k", "-ar", "44100", "-ac", "2"]
            if bridge.preview:
                # Optional local ffplay preview avoids a DLNA receiver's HLS
                # buffer. Loopback only; it is not advertised as a TV protocol.
                self.preview = subprocess.Popen([bridge.ffplay, "-loglevel", "error", "-fflags", "nobuffer",
                    "-flags", "low_delay", "-framedrop", "-window_title", "C1 Max game preview",
                    "-i", f"udp://127.0.0.1:{bridge.preview_port}?fifo_size=4096&overrun_nonfatal=1"],
                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                target = f"[f=hls:hls_time=1:hls_list_size=4:hls_flags=delete_segments+omit_endlist+independent_segments:hls_segment_filename={self.directory}/segment%06d.ts]{self.directory}/stream.m3u8|[f=mpegts:onfail=ignore]udp://127.0.0.1:{bridge.preview_port}?pkt_size=1316"
                command += ["-f", "tee", target]
            else:
                command += ["-f", "hls", "-hls_time", "1", "-hls_list_size", "4",
                    "-hls_flags", "delete_segments+omit_endlist+independent_segments",
                    "-hls_segment_filename", str(self.directory / "segment%06d.ts"),
                    str(self.directory / "stream.m3u8")]
            self.process = subprocess.Popen(command, pass_fds=self.readers,
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=self.log)
            for fd in self.readers:
                os.close(fd)
            self.readers.clear()
            for fd in self.writers:
                os.set_blocking(fd, False)
            for method, fd in ((self.video_writer, video_w), (self.audio_writer, audio_w)):
                thread = threading.Thread(target=method, args=(fd,), daemon=True)
                self.threads.append(thread)
                thread.start()
        except BaseException:
            self.close()
            raise

    def feed(self, kind, payload):
        with self.lock:
            self.last_input = time.monotonic()
            if kind == b"V":
                self.frame = payload  # One latest frame; never queue old video.
                self.frames += 1
                self.last_video_time = self.last_input
                if self.first_video_time is None:
                    self.first_video_time = self.last_input
                self.started.set()
            elif kind == b"A":
                self.audio_received += len(payload)
                self.audio.extend(payload)
                if len(self.audio) > AUDIO_LIMIT:
                    remove = len(self.audio) - AUDIO_LIMIT
                    self.dropped_audio += remove
                    del self.audio[:remove]

    def write(self, fd, data):
        import select
        view = memoryview(data)
        deadline = time.monotonic() + 2
        while view and not self.closed.is_set():
            if time.monotonic() > deadline or self.process.poll() is not None:
                raise BrokenPipeError("Encoder stalled")
            if not select.select([], [fd], [], 0.1)[1]:
                continue
            try:
                written = os.write(fd, view)
                view = view[written:]
            except BlockingIOError:
                continue

    def paced(self, interval, produce, fd):
        while not self.started.wait(0.1):
            if self.closed.is_set():
                return
        deadline = time.monotonic()
        try:
            while not self.closed.is_set():
                self.write(fd, produce())
                deadline += interval
                now = time.monotonic()
                if now - deadline > 0.25:
                    deadline = now  # Never replay a backlog after host suspend.
                self.closed.wait(max(0, deadline - now))
        except (BrokenPipeError, OSError) as error:
            if not self.closed.is_set():
                print(f"Game encoder pipe failed: class={type(error).__name__} encoder_status={self.process.poll()}", flush=True)
            self.closed.set()

    def video_writer(self, fd):
        def frame():
            with self.lock:
                return self.frame
        self.paced(1 / FPS, frame, fd)

    def audio_writer(self, fd):
        def samples():
            size = 441 * 4  # 10ms, stereo 44100Hz.
            with self.lock:
                data = bytes(self.audio[:size])
                del self.audio[:size]
            return data + bytes(size - len(data))
        self.paced(0.01, samples, fd)

    def ready(self):
        return (self.directory / "stream.m3u8").is_file() and bool(list(self.directory.glob("segment*.ts")))

    def healthy(self):
        return not self.closed.is_set() and self.process.poll() is None and time.monotonic() - self.last_input < 5

    def close(self):
        with self.close_lock:
            if self.disposed:
                return
            self.disposed = True
        self.closed.set()
        for process in (self.preview, self.process):
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=2)
        for thread in self.threads:
            thread.join(timeout=2)
        for fd in self.writers + self.readers:
            with contextlib.suppress(OSError):
                os.close(fd)
        self.writers.clear()
        self.readers.clear()
        self.log.close()
        shutil.rmtree(self.directory, ignore_errors=True)


class Bridge:
    def __init__(self, config, ffmpeg, preview=False, ffplay=None, preview_port=28768):
        self.config, self.ffmpeg = config, ffmpeg
        self.preview, self.ffplay, self.preview_port = preview, ffplay, preview_port
        self.lock = threading.Lock()
        self.session = None

    def begin(self, app, title):
        with self.lock:
            if self.session is not None:
                raise ValueError("Another game is already streaming")
            self.session = Session(self, app, title)
            return self.session

    def end(self, session):
        with self.lock:
            if self.session is not session:
                return
            self.session = None
        session.close()
        elapsed = max(0.001, (session.last_video_time or 0) - (session.first_video_time or 0))
        print(f"Game stream ended: app={session.app} frames={session.frames} capture_fps={max(0, session.frames - 1) / elapsed:.1f} audio_bytes={session.audio_received} dropped_audio_bytes={session.dropped_audio}", flush=True)


class LimitedServer(socketserver.ThreadingMixIn):
    daemon_threads = True
    allow_reuse_address = True

    def __init__(self, *args, **kwargs):
        self.slots = threading.BoundedSemaphore(8)
        super().__init__(*args, **kwargs)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, *args):
        try:
            super().process_request_thread(*args)
        finally:
            self.slots.release()


class ControlServer(LimitedServer, socketserver.TCPServer):
    pass


class ControlHandler(socketserver.BaseRequestHandler):
    def handle(self):
        bridge, sock = self.server.bridge, self.request
        session = None
        phase = "handshake"
        try:
            sock.settimeout(3)
            header = bytearray()
            while len(header) < 4096:
                byte = receive(sock, 1)
                if byte == b"\n":
                    break
                header.extend(byte)
            else:
                raise ValueError("Handshake too large")
            hello = json.loads(header)
            if not isinstance(hello, dict) or not hmac.compare_digest(str(hello.get("token", "")), bridge.config["token"]):
                raise ValueError("Authentication failed")
            expected = {"protocol": PROTOCOL, "width": WIDTH, "height": HEIGHT,
                        "fps": FPS, "format": "rgb565le", "sample_rate": 44100, "channels": 2}
            if any(hello.get(key) != value for key, value in expected.items()):
                raise ValueError("Unsupported stream format")
            if hello.get("app") not in ("nes", "pcsx4all", "dosbox") or not isinstance(hello.get("title"), str):
                raise ValueError("Invalid game")
            session = bridge.begin(hello["app"], hello["title"][:256])
            phase = "media"
            self.reply({"ok": True, "protocol": PROTOCOL, "ready": False})
            last_reply = time.monotonic()
            while session.healthy():
                packet = receive(sock, 8)
                kind, reserved, length = packet[:1], packet[1:4], struct.unpack("!I", packet[4:])[0]
                if reserved != b"\0\0\0" or not ((kind == b"V" and length == FRAME_BYTES) or (kind == b"A" and 0 < length <= 16384 and length % 4 == 0) or (kind == b"P" and length == 0)):
                    raise ValueError("Invalid media packet")
                session.feed(kind, receive(sock, length))
                if time.monotonic() - last_reply >= 0.5:
                    status = {"ok": True, "protocol": PROTOCOL, "ready": session.ready()}
                    if status["ready"]:
                        status["url"] = f'http://{bridge.config["host"]}:{bridge.config["http_port"]}/session/{session.key}/stream.m3u8'
                    self.reply(status)
                    last_reply = time.monotonic()
            print(f"Game stream unhealthy: encoder_status={session.process.poll()} pipe_closed={session.closed.is_set()} input_gap={time.monotonic() - session.last_input:.1f}s frames={session.frames}", flush=True)
            self.reply({"ok": False, "error": "Encoder or input stopped"})
        except (EOFError, OSError, ValueError, TypeError, KeyError) as error:
            # Classes/phase/counters help diagnose transport failures without
            # logging request JSON, credentials, game titles or media URLs.
            print(f"Game connection ended: phase={phase} class={type(error).__name__} frames={session.frames if session else 0}", flush=True)
            with contextlib.suppress(OSError):
                self.reply({"ok": False, "error": "Invalid or disconnected stream"})
        finally:
            if session is not None:
                bridge.end(session)

    def reply(self, message):
        self.request.sendall(json.dumps(message, ensure_ascii=True).encode() + b"\n")


class MediaServer(LimitedServer, http.server.HTTPServer):
    pass


class MediaHandler(http.server.BaseHTTPRequestHandler):
    def setup(self):
        super().setup()
        self.connection.settimeout(3)

    def log_message(self, *_args):
        pass  # Never write bearer capability URLs to logs.

    def do_HEAD(self):
        self.media(False)

    def do_GET(self):
        self.media(True)

    def media(self, body):
        self.connection.settimeout(3)
        path = re.fullmatch(r"/session/([a-f0-9]{48})/(stream\.m3u8|segment[0-9]{6,10}\.ts)", self.path)
        session = self.server.bridge.session
        if not path or session is None or not hmac.compare_digest(path[1], session.key):
            self.send_error(404)
            return
        try:
            data = (session.directory / path[2]).read_bytes()
        except OSError:
            self.send_error(404)
            return
        if len(data) > 2 * 1024 * 1024:
            self.send_error(503)
            return
        begin, end, code = 0, len(data) - 1, 200
        requested = self.headers.get("Range")
        if requested:
            match = re.fullmatch(r"bytes=(\d+)-(\d*)", requested)
            if not match:
                self.send_error(416)
                return
            begin = int(match[1])
            end = min(end, int(match[2])) if match[2] else end
            if begin > end or begin >= len(data):
                self.send_error(416)
                return
            code = 206
        self.send_response(code)
        self.send_header("Content-Type", "application/vnd.apple.mpegurl" if path[2].endswith("m3u8") else "video/mp2t")
        self.send_header("Content-Length", str(end - begin + 1))
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Cache-Control", "no-store")
        self.send_header("Access-Control-Allow-Origin", "*")
        if code == 206:
            self.send_header("Content-Range", f"bytes {begin}-{end}/{len(data)}")
        self.end_headers()
        if body:
            with contextlib.suppress(OSError):
                self.wfile.write(data[begin:end + 1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=CONFIG)
    parser.add_argument("--bind", default="0.0.0.0")
    parser.add_argument("--preview", action="store_true", help="Open task-owned low-buffer ffplay window; Kodi remains a separate receiver")
    args = parser.parse_args()
    config = load_config(args.config)
    ffmpeg, ffplay = shutil.which("ffmpeg"), shutil.which("ffplay")
    if not ffmpeg or (args.preview and not ffplay):
        parser.error("Install ffmpeg (and ffplay for --preview), e.g. brew install ffmpeg")
    bridge = Bridge(config, ffmpeg, args.preview, ffplay)
    control = ControlServer((args.bind, config["port"]), ControlHandler)
    media = MediaServer((args.bind, config["http_port"]), MediaHandler)
    control.bridge = media.bridge = bridge
    workers = [threading.Thread(target=server.serve_forever, daemon=True) for server in (control, media)]
    for thread in workers:
        thread.start()
    print(f'C1 Max bridge ready on {config["host"]}:{config["port"]}; only the paired device can publish', flush=True)
    def terminate(_signum, _frame):
        raise KeyboardInterrupt()
    previous_term = signal.signal(signal.SIGTERM, terminate)
    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        signal.signal(signal.SIGTERM, previous_term)
        for server in (control, media):
            server.shutdown()
            server.server_close()
        if bridge.session is not None:
            bridge.end(bridge.session)
        for thread in workers:
            thread.join(timeout=3)


if __name__ == "__main__":
    main()
