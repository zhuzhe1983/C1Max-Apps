#!/usr/bin/env python3
"""用真实 MIPS 播放器和校验请求头的本机服务复现 Bilibili 403。"""
import argparse
import http.server
import json
import pathlib
import shlex
import subprocess
import threading

ROOT=pathlib.Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url',default='http://127.0.0.1:8775')
    args=parser.parse_args();fixture=ROOT/'build/debug/bilibili/header-tone.mp3'
    fixture.parent.mkdir(parents=True,exist_ok=True)
    if not fixture.exists():subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-f','lavfi','-i','sine=frequency=440:duration=0.3','-ac','2','-ar','48000',str(fixture)],check=True)
    requests=[]
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def do_GET(self):
            allowed=self.headers.get_all('Referer')==['https://www.bilibili.com/'] and self.headers.get_all('User-Agent')==['Mozilla/5.0']
            requests.append({'referer_ok':self.headers.get('Referer')=='https://www.bilibili.com/','user_agent_ok':self.headers.get('User-Agent')=='Mozilla/5.0','status':200 if allowed else 403})
            data=fixture.read_bytes() if allowed else b'Forbidden: missing playback headers'
            self.send_response(200 if allowed else 403);self.send_header('Content-Length',str(len(data)));self.end_headers();self.wfile.write(data)
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=server.serve_forever,daemon=True).start()
    try:
        port=server.server_address[1]
        explicit=['-user-agent','Mozilla/5.0','-referrer','https://www.bilibili.com/']
        fallback=['-lavfstreamopts','user_agent=Mozilla/5.0,referer=https://www.bilibili.com/']
        cases={'original':explicit+fallback,'explicit':explicit,'lavf_only':fallback,
               'order_independent':['-lavfstreamopts','user_agent=wrong,referer=https://invalid.example/']+explicit}
        reports=[]
        for name,headers in cases.items():
            requests.clear()
            options=['/usr/bin/mplayer','-noconfig','all','-slave','-quiet','-identify','-noconsolecontrols','-nolirc','-nojoystick','-nomouseinput','-nosub','-noautosub','-osdlevel','0','-cache','512','-cache-min','10','-framedrop','-vo','null','-ao','null',*headers,f'http://10.0.2.2:{port}/clip.mp3']
            result=subprocess.run(['python3',str(ROOT/'scripts/console.py'),'--url',args.url,'--timeout','15',shlex.join(options)],capture_output=True,text=True,timeout=20)
            report={'case':name,'requests':list(requests),'guest_exit':result.returncode,'playback_headers_ok':bool(requests) and all(r['status']==200 for r in requests),'guest_reported_403':'403 Forbidden' in result.stdout or '403: Forbidden' in result.stdout}
            print(json.dumps(report,ensure_ascii=False),flush=True);reports.append(report)
            assert report['playback_headers_ok'] and report['guest_exit']==0 and not report['guest_reported_403'],'实际 MIPS 播放器未正常携带 Bilibili 请求头播放'
        (fixture.parent/'headers-results.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2)+'\n')
    finally:server.shutdown();server.server_close()

if __name__=='__main__':main()
