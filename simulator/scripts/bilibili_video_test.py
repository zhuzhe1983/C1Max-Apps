#!/usr/bin/env python3
"""在独立测试实例中验证官方 Bilibili ELF 的视频、暂停及跳转。"""
import argparse
import base64
import hashlib
import http.server
import json
import pathlib
import re
import subprocess
import threading
import time
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True, help='独立测试盘的模拟器地址，如 http://127.0.0.1:8776')
    parser.add_argument('--min-speed', type=float, default=0, help='要求的播放速度，1 表示实时；0 仅验证功能')
    parser.add_argument('--min-fps', type=float, default=0, help='要求的实际接收帧率；0 仅验证功能')
    args = parser.parse_args(); url = args.url.rstrip('/')
    destination = ROOT / 'build/debug/bilibili'; destination.mkdir(parents=True, exist_ok=True)
    fixture = destination / 'fixture.mp4'
    if not fixture.exists():
        subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i',
                        'testsrc2=size=640x360:rate=10:duration=30', '-f', 'lavfi', '-i',
                        'sine=frequency=880:sample_rate=48000:duration=30', '-c:v', 'libx264',
                        '-preset', 'ultrafast', '-g', '10', '-bf', '2', '-pix_fmt', 'yuv420p',
                        '-c:a', 'aac', '-ac', '2', '-b:a', '96k', '-movflags', '+faststart',
                        '-shortest', str(fixture)], check=True)
    def get(path):
        with urllib.request.urlopen(url + path, timeout=10) as response: return json.load(response)
    token = get('/api/state')['token']
    def post(path, data):
        request = urllib.request.Request(url + path, json.dumps(data).encode(),
                    {'Content-Type': 'application/json', 'X-C1Sim-Token': token})
        with urllib.request.urlopen(request, timeout=120) as response: return json.load(response)
    def serial(command):
        result = subprocess.run(['python3', str(ROOT/'scripts/console.py'), '--url', url,
                                 '--timeout', '20', command], capture_output=True, text=True, timeout=25)
        assert result.returncode == 0, result.stdout + result.stderr
        return result.stdout
    def key(code):
        for value in (1, 0):
            post('/api/key', {'source': 0, 'code': code, 'value': value}); time.sleep(.08)
    def image(name):
        with urllib.request.urlopen(url + '/api/frame.png') as response:
            (destination / name).write_bytes(response.read())
    def picture_hash():
        pixels = base64.b64decode(get('/api/frame')['pixels'])
        # 中央画面区域；避开播放器控件，按原生 340x800 framebuffer 取样。
        data = b''.join(pixels[y*1360+60*4:y*1360+210*4] for y in range(50, 750))
        return hashlib.sha256(data).hexdigest()
    def stats():
        output = serial("tail -n 15 /tmp/c1-bili-qa.log")
        matches = re.findall(r'frames=(\d+) elapsed_ms=(\d+) position_ms=(\d+) paused=(\d+)', output)
        assert matches, '原始播放器没有产生帧统计：' + output
        return dict(zip(('frames', 'elapsed_ms', 'position_ms', 'paused'), map(int, matches[-1])))
    def wait_stats(predicate, timeout=30):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            current = stats()
            if predicate(current): return current
            time.sleep(1)
        raise AssertionError('播放器状态未达到预期：' + str(current))
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            data = fixture.read_bytes(); self.send_response(200)
            self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    try:
        post('/api/install', {'id': 'bilibili'}); post('/api/run', {'id': 'bilibili'})
        for _ in range(80):
            state = get('/api/state')
            if state['app'] == 'bilibili' and state['pid']: break
            time.sleep(.1)
        assert state['app'] == 'bilibili' and state['pid'], '原始 Bilibili 无法启动'
        serial(f"kill {state['pid']}; /usr/bin/wget -q -O /tmp/c1-bili-direct.mp4 http://10.0.2.2:{server.server_address[1]}/fixture.mp4")
        serial('C1_APPS_DATA=/tmp/c1-bili-qa C1_APPS_ROOT=/storage/apps/data/appstore/current C1_BILI_QA_LOCAL=1 /storage/apps/data/appstore/current/bilibili/c1max-bilibili > /tmp/c1-bili-qa.log 2>&1 & echo $! > /tmp/c1-bili-qa.pid')
        time.sleep(6); playing = stats(); assert playing['frames'] >= 10 and playing['position_ms'] > 1000, playing
        progress = wait_stats(lambda s: s['elapsed_ms'] >= playing['elapsed_ms'] + 4000)
        speed = (progress['position_ms'] - playing['position_ms']) / (progress['elapsed_ms'] - playing['elapsed_ms'])
        fps = 1000 * (progress['frames'] - playing['frames']) / (progress['elapsed_ms'] - playing['elapsed_ms'])
        print(json.dumps({'playback_speed': speed, 'min_speed': args.min_speed, 'frames': progress['frames'], 'actual_fps': fps}, ensure_ascii=False), flush=True)
        assert speed >= args.min_speed, '播放速度低于实时验收要求'
        assert fps >= args.min_fps, '实际视频帧率低于验收要求'
        image('video-playing.png'); first = picture_hash(); deadline = time.monotonic() + 2
        while first == picture_hash() and time.monotonic() < deadline: time.sleep(.2)
        assert first != picture_hash(), '视频区域没有变化'
        key(28); paused = wait_stats(lambda s: s['paused'] == 1)
        first = picture_hash(); time.sleep(.7); assert first == picture_hash(), '暂停后视频仍在变化'
        key(32); sought = wait_stats(lambda s: s['paused'] == 1 and s['position_ms'] >= paused['position_ms'] + 9000 and s['frames'] > paused['frames'])
        image('video-paused-seek.png'); key(28)
        resumed = wait_stats(lambda s: s['paused'] == 0 and s['frames'] > sought['frames'] + 5)
        key(30); backward = wait_stats(lambda s: s['frames'] > resumed['frames'] and s['position_ms'] < resumed['position_ms'])
        report = {'original_bilibili': '0.2.0', 'fixture': 'H.264/AAC 640x360 10fps', 'playback_speed': speed, 'actual_fps': fps,
                  'playing': playing, 'paused': paused, 'paused_seek': sought, 'resumed': resumed,
                  'backward_seek': backward, 'picture_changes': True, 'pause_picture_stable': True}
        (destination/'video-results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
        print(json.dumps(report, ensure_ascii=False), flush=True)
    finally:
        serial('if [ -f /tmp/c1-bili-qa.pid ]; then kill "$(cat /tmp/c1-bili-qa.pid)"; rm -f /tmp/c1-bili-qa.pid; fi')
        post('/api/run', {'id': 'launcher'}); server.shutdown(); server.server_close()

if __name__ == '__main__': main()
