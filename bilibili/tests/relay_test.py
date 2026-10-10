#!/usr/bin/env python3
"""Isolated real-wget/TLS/HTTP relay checks, no Bilibili or receiver traffic.
Run in Linux with fixture.bilivideo.com mapped to 127.0.0.1 and TLS port 443 free.
"""
import contextlib
import http.client
import http.server
import os
from pathlib import Path
import select
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import urlsplit

PAYLOAD = bytes(range(256)) * 512
observed = []
class Source(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args): pass
    def do_HEAD(self):
        self.send_response(200);self.send_header('Content-Length',str(len(PAYLOAD)));self.end_headers()
    def do_GET(self):
        observed.append((self.path, dict(self.headers)))
        assert self.headers.get('Referer') == 'https://www.bilibili.com/'
        assert self.headers.get('User-Agent') == 'Mozilla/5.0'
        assert 'Cookie' not in self.headers
        if self.path.startswith('/redirect'):
            self.send_response(302); self.send_header('Location', 'https://example.invalid/private'); self.send_header('Content-Length', '0'); self.end_headers(); return
        start, end = 0, len(PAYLOAD)-1
        code = 200
        if self.headers.get('Range'):
            first,last=self.headers['Range'][6:].split('-')
            start,end=(int(first),int(last) if last else end) if first else (len(PAYLOAD)-int(last),end)
            code=206
        data=PAYLOAD[start:end+1]
        self.send_response(code); self.send_header('Content-Type','video/mp4'); self.send_header('Content-Length',str(len(data)))
        self.send_header('Set-Cookie', 'private=must-not-leak')
        if code==206:self.send_header('Content-Range',f'bytes {start}-{end}/{len(PAYLOAD)}')
        self.end_headers()
        with contextlib.suppress(OSError): self.wfile.write(data)

@contextlib.contextmanager
def relay(binary, path='/video.mp4?private=signed', env=None):
    p=subprocess.Popen([binary,'https://fixture.bilivideo.com'+path],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,env=env)
    try:
        ready,_,_=select.select([p.stdout],[],[],5)
        assert ready,'relay startup timeout'
        url=p.stdout.readline().strip();assert url.startswith('http://127.0.0.1:'),p.stderr.read() if p.poll() is not None else url
        yield url
    finally:
        if p.poll() is None:
            p.stdin.write('\n');p.stdin.flush()
        try:p.wait(timeout=5)
        except subprocess.TimeoutExpired:p.kill();p.wait();raise AssertionError('relay failed to stop promptly')
        assert p.returncode==0,p.stderr.read()
        p.stdin.close();p.stdout.close();p.stderr.close()

def request(url, method='GET', headers=None, path=None, source=None):
    u=urlsplit(url);c=http.client.HTTPConnection(u.hostname,u.port,timeout=20,source_address=(source,0) if source else None)
    try:
        c.request(method,path or u.path,headers=headers or {});r=c.getresponse();body=r.read();return r.status,dict(r.getheaders()),body
    finally:c.close()

def main():
    binary=os.path.abspath(sys.argv[1])
    before=set(Path('/tmp').glob('c1-bili-relay-*'))
    with tempfile.TemporaryDirectory(prefix='bili-relay-test-') as work:
        root=Path(work);(root/'shared').mkdir()
        cert=root/'shared/ca-certificates.crt';key=root/'key.pem'
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-keyout',str(key),'-out',str(cert),'-subj','/CN=fixture.bilivideo.com','-addext','subjectAltName=DNS:fixture.bilivideo.com'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        server=http.server.ThreadingHTTPServer(('127.0.0.1',443),Source);server.daemon_threads=True
        context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(cert,key);server.socket=context.wrap_socket(server.socket,server_side=True)
        thread=threading.Thread(target=server.serve_forever);thread.start();env=dict(os.environ,C1_APPS_ROOT=work)
        try:
            with relay(binary,env=env) as url:
                code,h,b=request(url);assert code==200 and b==PAYLOAD and 'Set-Cookie' not in h
                assert request(url,'HEAD')[2]==b''
                code,h,b=request(url,headers={'Range':'bytes=9-31'});assert code==206 and b==PAYLOAD[9:32] and h.get('Content-Range')==f'bytes 9-31/{len(PAYLOAD)}',(code,h,len(b),observed[-1])
                assert request(url,headers={'Range':'bytes=-16'})[2]==PAYLOAD[-16:]
                assert request(url,headers={'Range':'bytes=131060-'})[2]==PAYLOAD[131060:]
                assert request(url,headers={'Range':'bytes=1-2,4-5'})[0]==416
                assert request(url,'POST')[0]==405
                assert request(url,path='/unknown.mp4')[0]==404
                assert request(url,source='127.0.0.2')[0]==403
                assert request(url,headers={'Content-Length':'1'})[0]==400
                assert all('private=signed' in p for p,h in observed)
                assert observed and all('Cookie' not in h for p,h in observed)
            with relay(binary,env=env) as url:assert request(url,headers={'Range':'bytes=-16'})[2]==PAYLOAD[-16:]
            with relay(binary,'/redirect',env=env) as url:assert request(url)[0]==502
            cert.write_text('invalid trust store')
            with relay(binary,env=env) as url:assert request(url)[0]==502
            with relay(binary,env=env) as url:
                u=urlsplit(url);held=[]
                try:
                    for _ in range(3):
                        client=socket.create_connection((u.hostname,u.port));client.sendall(b'GET ');held.append(client)
                    time.sleep(.3)
                    assert request(url)[0]==503
                finally:
                    for client in held:client.close()
            with relay(binary,env=env) as url:
                u=urlsplit(url);client=socket.create_connection((u.hostname,u.port));client.sendall(b'GET ')
                time.sleep(.1);stop_start=time.monotonic()
            client.close();assert time.monotonic()-stop_start<2,'inflight shutdown exceeded bound'
            denied=subprocess.run([binary,'https://example.com/video.mp4'],input='\n',capture_output=True,text=True,env=env)
            assert denied.returncode!=0 and not denied.stdout
            assert set(Path('/tmp').glob('c1-bili-relay-*'))==before,'private relay temp files leaked'
            print('PASS relay: real HTTPS trust/Referer, MP4 GET/HEAD/Range, peer+token, redirects/cookies/body rejection, TLS rejection, three-client bound, cancelled inflight request, cleanup')
        finally:server.shutdown();server.server_close();thread.join()
if __name__=='__main__':main()
