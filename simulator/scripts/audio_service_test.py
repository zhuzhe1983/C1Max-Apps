#!/usr/bin/env python3
"""通过真实 MIPS IPC 客户端验证更新后的共享音频服务。请对独立测试盘运行。"""
import argparse
import http.server
import json
import pathlib
import re
import subprocess
import sys
import threading

ROOT = pathlib.Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__); parser.add_argument('--url',required=True); args=parser.parse_args()
    directory=ROOT/'build/debug/audio-service'; directory.mkdir(parents=True,exist_ok=True)
    probe=directory/'audio-service-probe'; assert probe.exists(), '先交叉编译 scripts/fixtures/audio-service-probe.c'
    tone=directory/'tone.mp3'
    if not tone.exists():
        subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','sine=frequency=660:duration=6','-ar','48000','-ac','2',str(tone)],check=True)
    class Source(http.server.BaseHTTPRequestHandler):
        def log_message(self,*_): pass
        def do_GET(self):
            data=(probe if self.path=='/probe' else tone).read_bytes()
            self.send_response(200); self.send_header('Content-Length',str(len(data))); self.end_headers()
            try: self.wfile.write(data)
            except OSError: pass
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Source);threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        port=server.server_address[1]
        command=f'mkdir -p /tmp/c1-audio-qa; /usr/bin/wget -q -O /tmp/audio-service-probe http://10.0.2.2:{port}/probe; chmod +x /tmp/audio-service-probe; C1_APPS_DATA=/tmp/c1-audio-qa /tmp/audio-service-probe http://10.0.2.2:{port}/tone.mp3'
        result=subprocess.run([sys.executable,str(ROOT/'scripts/console.py'),'--url',args.url,'--timeout','15',command],capture_output=True,text=True,timeout=20)
        match=re.search(r'\{"playing":.*?\}',result.stdout)
        assert match and result.returncode==0, 'MIPS 共享音频服务验证失败：'+result.stdout+result.stderr
        report=json.loads(match.group());assert report['playing'] and report['pause_resume'] and report['shutdown'],report
        (directory/'results.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report),flush=True)
    finally: server.shutdown();server.server_close()

if __name__=='__main__': main()
