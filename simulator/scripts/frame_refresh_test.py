#!/usr/bin/env python3
"""测量真实 MIPS framebuffer 到主机的连续画面采集速度；只在独立测试盘运行。"""
import argparse
import http.server
import json
import pathlib
import subprocess
import threading
import time
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--min-fps', type=float, default=25)
    args = parser.parse_args(); url = args.url.rstrip('/')
    binary = ROOT/'build/debug/display/frame-probe'
    assert binary.exists(), '先交叉编译 scripts/fixtures/frame-probe.c 到 build/debug/display/frame-probe'
    def state():
        with urllib.request.urlopen(url+'/api/state', timeout=5) as r: return json.load(r)
    token = state()['token']
    def run():
        request = urllib.request.Request(url+'/api/run', b'{"id":"launcher"}', {'Content-Type':'application/json','X-C1Sim-Token':token})
        with urllib.request.urlopen(request) as r: r.read()
    def serial(command):
        subprocess.run(['python3', str(ROOT/'scripts/console.py'), '--url', url, '--timeout', '15', command], capture_output=True, check=True)
    class Source(http.server.BaseHTTPRequestHandler):
        def log_message(self, *_): pass
        def do_GET(self):
            data=binary.read_bytes(); self.send_response(200); self.send_header('Content-Length', str(len(data))); self.end_headers(); self.wfile.write(data)
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Source)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        run(); time.sleep(1); assert state()['pid']
        # 保持正常前台应用的日志管道打开，避免已退出应用的 POLLHUP 触发空转。
        serial(f'/usr/bin/wget -q -O /tmp/frame-probe http://10.0.2.2:{server.server_address[1]}/probe; chmod +x /tmp/frame-probe; /tmp/frame-probe > /tmp/frame-probe.log 2>&1 & echo $! > /tmp/frame-probe.pid')
        time.sleep(1); first=state()['sequence']; begin=time.monotonic(); time.sleep(5)
        last=state()['sequence']; elapsed=time.monotonic()-begin
        report={'sample_seconds':elapsed,'frames':last-first,'capture_fps':(last-first)/elapsed,'source_target_fps':60}
        (binary.parent/'capture-results.json').write_text(json.dumps(report,indent=2)+'\n')
        print(json.dumps(report),flush=True)
        assert report['capture_fps']>=args.min_fps, '屏幕采集刷新率低于验收要求'
    finally:
        serial('if [ -f /tmp/frame-probe.pid ]; then kill "$(cat /tmp/frame-probe.pid)"; rm /tmp/frame-probe.pid; fi')
        run(); server.shutdown(); server.server_close()

if __name__ == '__main__': main()
