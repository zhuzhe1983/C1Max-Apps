#!/usr/bin/env python3
import base64
import http.server
import pathlib
import subprocess
import sys
import tempfile
import threading
import time

FEED=b'<feed xmlns="http://www.w3.org/2005/Atom"><title>Test</title><entry><title>Book</title><link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/book"/></entry></feed>'
BOOK=b'PK\x03\x04'+b'original book bytes '*100
requests=[]
class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_GET(self):
        requests.append((self.server.server_port,self.path,self.headers.get('Authorization')))
        if self.path in ['/redirect','/cross','/loop']:
            target='/feed' if self.path=='/redirect' else ('http://127.0.0.1:'+str(other.server_port)+'/feed' if self.path=='/cross' else '/loop')
            self.send_response(302);self.send_header('Location',target);self.send_header('Content-Length','0');self.end_headers();return
        body=FEED if self.path=='/feed' else BOOK
        if self.path=='/html':body=b'<!DOCTYPE html><html>login</html>'
        self.send_response(200)
        length=200*1024*1024 if self.path=='/large' else len(body)+(1000 if self.path=='/short' else 0)
        if self.path!='/slow':self.send_header('Content-Length',str(length))
        self.end_headers()
        try:
            if self.path=='/slow':
                for _ in range(40):self.wfile.write(body);self.wfile.flush();time.sleep(.1)
            else:self.wfile.write(body)
        except (BrokenPipeError,ConnectionResetError):pass

def server():
    s=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler)
    threading.Thread(target=s.serve_forever,daemon=True).start();return s
primary,other=server(),server()
try:
    with tempfile.TemporaryDirectory(prefix='c1-opds-test-') as directory:
        folder=pathlib.Path(directory)
        def run(mode,path,auth=False,cancel=None,ok=True):
            args=[sys.argv[1],mode,f'http://127.0.0.1:{primary.server_port}'+path,directory]
            if auth or cancel is not None:args += ['test' if auth else '', 'secret' if auth else '']
            if cancel is not None:args += [str(cancel)]
            r=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,timeout=12)
            assert (r.returncode==0)==ok,(path,r.returncode,r.stdout,r.stderr)
            assert not list(folder.glob('.crosspoint-transfer-*')),'Temporary transfer not removed'
            return r
        run('feed','/feed')
        assert requests[-1][2] is None
        run('feed','/redirect',auth=True)
        expected='Basic '+base64.b64encode(b'test:secret').decode()
        assert requests[-1][2]==expected
        run('feed','/cross',auth=True)
        assert requests[-1][0]==other.server_port and requests[-1][2] is None
        run('feed','/loop',ok=False)
        run('download','/book');run('download','/book')
        assert sorted(p.name for p in folder.iterdir())==['book (1).epub','book.epub']
        assert all(p.read_bytes()==BOOK for p in folder.iterdir())
        for bad in ['/short','/html','/large']:run('download',bad,ok=False)
        started=time.monotonic();run('download','/slow',cancel=200,ok=False)
        assert time.monotonic()-started<3,'Cancellation did not stop child promptly'
        assert len(list(folder.iterdir()))==2,'Failed/cancelled transfer published a book'
        print('HTTP redirect/auth scope, exact downloads, duplicate preservation, partial/HTML/oversize rejection and cancellation passed')
finally:
    primary.shutdown();other.shutdown();primary.server_close();other.server_close()
