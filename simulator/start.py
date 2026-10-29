#!/usr/bin/env python3
"""C1 Max QEMU 系统模拟器；Python 标准库提供本机界面。"""
import argparse
import array
import collections
import http.server
import json
import os
import pathlib
import secrets
import shutil
import signal
import socket
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
import urllib.error
import webbrowser
import zlib

ROOT=pathlib.Path(__file__).resolve().parent
FRAME_BYTES=340*800*4


def png(pixels,portrait=False):
    source=array.array('I');source.frombytes(pixels)
    rows=[]
    width,height=(340,800) if portrait else (800,340)
    for y in range(height):
        row=bytearray(source[y*340:(y+1)*340].tobytes() if portrait else source[y::340][::-1].tobytes())
        row[0::4],row[2::4]=row[2::4],row[0::4]
        row[3::4]=b'\xff'*width
        rows.append(b'\0'+row)
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows),1))+chunk(b'IEND',b'')


class Simulator:
    def __init__(self):
        self.condition=threading.Condition();self.send_lock=threading.Lock();self.console_lock=threading.Lock()
        self.connection=None;self.console=None;self.ready=False;self.running=True;self.app='none';self.pid=0;self.exit_code=None
        self.console_log=ROOT/'runtime/boot.log'
        self.sequence=0;self.pixels=bytes(FRAME_BYTES);self.portrait=False;self.image=png(self.pixels)
        self.logs=collections.deque(maxlen=80);self.token=secrets.token_urlsafe(32);self.revision=0;self.shutdown_done=threading.Event()
        self.apps=json.loads((ROOT/'build/apps.json').read_text())
    def state(self):
        with self.condition:return {'ready':self.ready,'app':self.app,'pid':self.pid,'exit_code':self.exit_code,'sequence':self.sequence,
            'portrait':self.portrait,'apps':self.apps,'logs':list(self.logs),'token':self.token}
    def update(self):self.revision+=1;self.condition.notify_all()
    def command(self,line):
        with self.send_lock:
            if not self.ready or not self.connection:raise ConnectionError('客体尚未连接')
            self.connection.sendall((line+'\n').encode())
    def receive(self,listener):
        try:
            connection,_=listener.accept();self.connection=connection
            with connection,connection.makefile('rb') as stream:
                while self.running:
                    line=stream.readline(4096)
                    if not line:break
                    if not line.endswith(b'\n'):raise ValueError('客体消息过长')
                    fields=line.decode('ascii').strip().split(' ')
                    content=None
                    if fields[0] in ('FRAME','LOG'):
                        length=int(fields[-1]);limit=FRAME_BYTES if fields[0]=='FRAME' else 2048
                        if length<0 or length>limit:raise ValueError('客体数据长度错误')
                        content=stream.read(length)
                        if len(content)!=length:raise ValueError('客体数据不完整')
                    with self.condition:
                        if fields[0]=='READY':self.ready=True
                        elif fields[0]=='INSTALLED':
                            for app in self.apps:
                                if app['id']==fields[1]:app['installed']=fields[2]=='1'
                        elif fields[0]=='APP':self.app=fields[1];self.pid=int(fields[2]);self.exit_code=None
                        elif fields[0]=='FRAME':
                            if len(content)!=FRAME_BYTES:raise ValueError('framebuffer 长度错误')
                            self.sequence=int(fields[1]);self.pixels=content;self.image=png(content,self.portrait)
                        elif fields[0]=='LOG':self.logs.append(content.decode('utf-8',errors='replace').rstrip())
                        elif fields[0]=='EXIT':self.exit_code=int(fields[1]);self.pid=0
                        elif fields[0]=='BYE':self.shutdown_done.set()
                        elif fields[0]=='ERROR':self.logs.append('客体：'+' '.join(fields[1:]))
                        self.update()
        except (OSError,ValueError) as error:
            with self.condition:self.logs.append(str(error))
        finally:
            self.connection=None
            with self.condition:self.ready=False;self.update()


def handler(simulator,port,proxy):
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version='HTTP/1.1'
        def log_message(self,*args):pass
        def reply(self,status,data,kind='application/json; charset=utf-8'):
            if not isinstance(data,bytes):data=json.dumps(data,ensure_ascii=False).encode()
            self.send_response(status);self.send_header('Content-Type',kind);self.send_header('Content-Length',str(len(data)))
            self.send_header('Cache-Control','no-store');self.send_header('X-Content-Type-Options','nosniff');self.end_headers();self.wfile.write(data)
        def do_GET(self):
            path=urllib.parse.urlsplit(self.path).path
            if path.startswith('/video/source/') and simulator.video:
                simulator.video.source(self,path.removeprefix('/video/source/'))
            elif path=='/api/state':self.reply(200,simulator.state())
            elif path=='/api/frame.png':
                with simulator.condition:image=simulator.image
                self.reply(200,image,'image/png')
            elif path=='/api/frame':
                import base64
                with simulator.condition:self.reply(200,{'sequence':simulator.sequence,'pixels':base64.b64encode(simulator.pixels).decode()})
            elif path=='/api/binaries':
                audits=json.loads((ROOT/'build/binaries.json').read_text())
                known={a['entry'] for a in audits}
                for audit in (ROOT/'build/payload').glob('*/c1sim-audit.json'):
                    row=json.loads(audit.read_text())
                    if row['entry'] not in known:audits.append(row)
                self.reply(200,audits)
            elif path=='/api/console':
                log=simulator.console_log;self.reply(200,{'text':log.read_bytes()[-65536:].decode('utf-8',errors='replace') if log.exists() else ''})
            elif path=='/api/events':
                self.send_response(200);self.send_header('Content-Type','text/event-stream');self.send_header('Connection','close');self.end_headers()
                revision=-1
                try:
                    while simulator.running:
                        with simulator.condition:
                            if revision==simulator.revision:simulator.condition.wait(timeout=15)
                            revision=simulator.revision;state=simulator.state()
                        self.wfile.write(('data: '+json.dumps(state,ensure_ascii=False)+'\n\n').encode());self.wfile.flush()
                except (OSError,ConnectionError):pass
                self.close_connection=True
            elif path.startswith('/repo/'):
                name=path.removeprefix('/repo/')
                ident=name.removesuffix('-runtime.tar.gz') if name.endswith('-runtime.tar.gz') else name.removesuffix('.tar.gz')
                if name.endswith('.tar.gz') and ident in [a['id'] for a in simulator.apps]:
                    target=ROOT/'build/repository'/name
                    self.reply(200,target.read_bytes(),'application/octet-stream') if target.exists() else self.reply(404,{'error':'包不存在'})
                else:self.reply(404,{'error':'路径不允许'})
            elif path in ('/','/index.html','/app.js','/style.css'):
                name='index.html' if path=='/' else path[1:]
                self.reply(200,(ROOT/'public'/name).read_bytes(),{'html':'text/html','js':'text/javascript','css':'text/css'}[name.rsplit('.',1)[-1]]+'; charset=utf-8')
            else:self.reply(404,{'error':'页面不存在'})
        def do_POST(self):
            if self.headers.get('Origin') not in (None,f'http://127.0.0.1:{port}',f'http://localhost:{port}') or self.headers.get('X-C1Sim-Token')!=simulator.token:
                self.reply(403,{'error':'会话令牌或来源无效'});return
            if self.path=='/network':
                self.network();return
            if self.path=='/video' and simulator.video:
                simulator.video.serve(self);return
            try:
                length=int(self.headers.get('Content-Length','0'))
                if length<1 or length>8192:raise ValueError('请求长度错误')
                data=json.loads(self.rfile.read(length))
                if self.path=='/api/key':
                    source,code,value=data.get('source',0),data['code'],data['value']
                    if any(type(v)is not int for v in (source,code,value)) or source not in (0,1) or not 0<=code<=767 or value not in (0,1,2):raise ValueError('按键参数错误')
                    simulator.command(f'KEY {source} {code} {value}')
                elif self.path=='/api/touch':
                    x,y,down=data['x'],data['y'],data['down']
                    width,height=(340,800) if simulator.portrait else (800,340)
                    if any(type(v)is not int for v in (x,y,down)) or not 0<=x<width or not 0<=y<height or down not in (0,1):raise ValueError('触摸参数错误')
                    nx,ny=(x,y) if simulator.portrait else (y,799-x)
                    simulator.command(f'TOUCH {nx} {ny} {down}')
                elif self.path=='/api/run':
                    if data.get('id') not in [a['id'] for a in simulator.apps]:raise ValueError('应用不存在')
                    simulator.command('RUN '+data['id'])
                elif self.path=='/api/rotate':
                    with simulator.condition:simulator.portrait=not simulator.portrait;simulator.image=png(simulator.pixels,simulator.portrait);simulator.update()
                elif self.path=='/api/install':
                    from scripts.install_app import install
                    ident=data['id']
                    if ident not in [a['id'] for a in simulator.apps] or ident=='launcher':raise ValueError('应用不存在')
                    with simulator.console_lock:
                        install(simulator,port,ident)
                elif self.path=='/api/console':
                    text=data['text']
                    if not isinstance(text,str) or len(text)>4096:raise ValueError('串口输入长度错误')
                    if not simulator.console:raise ConnectionError('串口尚未连接')
                    simulator.console.write(text.encode());simulator.console.flush()
                elif self.path=='/api/shutdown':
                    self.reply(200,{'ok':True});threading.Thread(target=self.server.shutdown,daemon=True).start();return
                else:self.reply(404,{'error':'接口不存在'});return
                self.reply(200,{'ok':True})
            except (OSError,ValueError,KeyError,TypeError,RuntimeError,subprocess.SubprocessError,StopIteration) as error:self.reply(400,{'error':str(error)})
        def network(self):
            try:
                url=self.headers['X-C1Max-URL'];address=urllib.parse.urlsplit(url)
                if address.scheme not in ('http','https') or not address.hostname or address.username or address.password or any(ord(c)<32 for c in url):raise ValueError('HTTP 地址无效')
                if address.hostname=='10.0.2.2':
                    url=urllib.parse.urlunsplit(address._replace(netloc='127.0.0.1'+(':'+str(address.port) if address.port else '')))
                length=int(self.headers.get('Content-Length','0'))
                if not 0<=length<=1048576:raise ValueError('请求体过大')
                method=self.headers.get('X-C1Max-Method','GET')
                if method not in ('GET','POST','PUT','DELETE','HEAD','PATCH'):raise ValueError('HTTP 方法无效')
                headers={k:v for k,v in self.headers.items() if k.lower() not in ('host','connection','content-length','origin') and not k.lower().startswith('x-c1')}
                body=self.rfile.read(length) if length else None
                class NoRedirect(urllib.request.HTTPRedirectHandler):
                    def redirect_request(self,*args,**kwargs):return None
                proxies={'https':proxy,'http':proxy} if proxy and address.hostname not in ('127.0.0.1','localhost','10.0.2.2') else {}
                handlers=[urllib.request.ProxyHandler(proxies)]
                if self.headers.get('X-C1Max-Redirect')=='0':handlers.append(NoRedirect())
                request=urllib.request.Request(url,body,headers,method=method)
                try:response=urllib.request.build_opener(*handlers).open(request,timeout=25)
                except urllib.error.HTTPError as error:response=error
                with response:
                    data=response.read(64*1024*1024+1)
                    if len(data)>64*1024*1024:raise ValueError('响应过大')
                    self.send_response(response.code)
                    self.send_header('Content-Length',str(len(data)));self.send_header('Content-Type',response.headers.get('Content-Type','application/octet-stream'))
                    self.send_header('Connection','close')
                    for cookie in response.headers.get_all('Set-Cookie',[]):self.send_header('Set-Cookie',cookie)
                    self.end_headers();self.wfile.write(data);self.close_connection=True
            except (OSError,ValueError,KeyError) as error:self.reply(502,{'error':'上游网络请求失败：'+type(error).__name__})
    return Handler


def audio_arguments(qemu,backend,recording):
    result=subprocess.run([qemu,'-audiodev','help'],capture_output=True,text=True,check=True)
    available=set(line.strip() for line in (result.stdout+result.stderr).splitlines())
    if recording:
        if backend not in ('auto','wav'):raise ValueError('录音仅可使用 wav 音频后端')
        if recording.exists():raise ValueError('录音文件已存在')
        recording.parent.mkdir(parents=True,exist_ok=True);backend='wav'
    if backend=='auto':
        candidates=['coreaudio'] if sys.platform=='darwin' else ['dsound','sdl'] if os.name=='nt' else ['pipewire','pa','alsa','sdl']
        backend=next((name for name in candidates if name in available),None)
    if backend not in available:raise ValueError('音频后端不可用，可使用 --audio-backend none')
    device=f'{backend},id=audio0,out.frequency=48000,out.channels=2,out.format=s16'
    if recording:device+=',path='+str(recording.resolve()).replace(',',',,')
    return ['-audiodev',device,'-device','AC97,audiodev=audio0'],backend


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',type=int,default=8775);parser.add_argument('--stop',action='store_true');parser.add_argument('--no-open',action='store_true')
    parser.add_argument('--disk',type=pathlib.Path,default=ROOT/'runtime/data.qcow2');parser.add_argument('--memory',type=int,default=128,choices=(128,256,512))
    parser.add_argument('--audio-backend',default='auto');parser.add_argument('--audio-record',type=pathlib.Path);parser.add_argument('--proxy')
    parser.add_argument('--video-backend',choices=('host','guest'),default='host')
    parser.add_argument('--gdb',type=int)
    args=parser.parse_args()
    if not 1<=args.port<=65535:parser.error('端口范围 1–65535')
    url=f'http://127.0.0.1:{args.port}'
    if args.stop:
        with urllib.request.urlopen(url+'/api/state',timeout=5) as response:token=json.load(response)['token']
        request=urllib.request.Request(url+'/api/shutdown',b'{}',{'Content-Type':'application/json','X-C1Sim-Token':token})
        with urllib.request.urlopen(request,timeout=5):pass
        for _ in range(100):
            try:
                with socket.create_connection(('127.0.0.1',args.port),timeout=.2):pass
            except ConnectionRefusedError:print('C1 Max 模拟器已关闭。');return
            except (TimeoutError,ConnectionResetError):pass
            time.sleep(.1)
        parser.error('关闭超时')
    qemu=shutil.which('qemu-system-mipsel');img=shutil.which('qemu-img')
    if not qemu or not img:parser.error('请安装 QEMU')
    if args.video_backend=='host' and (not shutil.which('ffmpeg') or not shutil.which('ffprobe')):
        parser.error('本机视频加速需要 ffmpeg 和 ffprobe；安装 ffmpeg，或使用 --video-backend guest')
    for name in ('vmlinux','rootfs.cpio.gz'):
        if not (ROOT/'build/images'/name).is_file():parser.error('请先执行 python3 scripts/build.py')
    audio,backend=audio_arguments(qemu,args.audio_backend,args.audio_record)
    if args.proxy:
        # Apply only to this simulator's downloads; loopback bypass is explicit.
        os.environ['https_proxy']=args.proxy;os.environ['http_proxy']=args.proxy
        os.environ['no_proxy']='127.0.0.1,localhost,10.0.2.2'
    (ROOT/'runtime').mkdir(exist_ok=True);args.disk.parent.mkdir(parents=True,exist_ok=True)
    new=not args.disk.exists()
    if new:subprocess.run([img,'create','-f','qcow2',str(args.disk),'512M'],check=True)
    simulator=Simulator();simulator.video=None
    if args.video_backend=='host':
        from video import VideoService
        simulator.video=VideoService(simulator.token,args.port,args.proxy)
    listener=socket.socket();listener.bind(('127.0.0.1',0));listener.listen(1)
    simulator.console_log=ROOT/'runtime'/('boot.log' if args.port==8775 else f'boot-{args.port}.log')
    qmp_listener=socket.socket();qmp_listener.bind(('127.0.0.1',0));qmp_listener.listen(1);qmp_listener.settimeout(10)
    server=http.server.ThreadingHTTPServer(('127.0.0.1',args.port),handler(simulator,args.port,args.proxy));server.daemon_threads=True
    threading.Thread(target=simulator.receive,args=(listener,),daemon=True).start()
    local=time.localtime()
    offset=-local.tm_gmtoff if hasattr(local,'tm_gmtoff') else time.altzone if local.tm_isdst else time.timezone
    timezone=f'SIM{"+" if offset>=0 else "-"}{abs(offset)//3600:02d}:{abs(offset)%3600//60:02d}'
    command=[qemu,'-M','malta','-cpu','24Kf','-m',str(args.memory),'-kernel',str(ROOT/'build/images/vmlinux'),'-initrd',str(ROOT/'build/images/rootfs.cpio.gz'),
        '-append',f'console=ttyS0 rdinit=/init c1sim_port={listener.getsockname()[1]} c1sim_http={args.port} c1sim_newdisk={int(new)} c1sim_token={simulator.token} c1sim_tz={timezone} c1sim_video={args.video_backend}',
        '-display','none','-serial','stdio','-monitor','none','-no-reboot','-qmp',f'tcp:127.0.0.1:{qmp_listener.getsockname()[1]}',
        '-netdev','user,id=net0','-device','pcnet,netdev=net0','-drive',f'file={args.disk.resolve()},format=qcow2,if=none,id=data','-device','virtio-blk-pci,drive=data',*audio]
    if args.gdb:
        if not 1<=args.gdb<=65535:parser.error('GDB 端口范围错误')
        command+=['-gdb',f'tcp:127.0.0.1:{args.gdb}']
    process=None;qmp=None
    def stop_signal(*_):raise KeyboardInterrupt()
    signal.signal(signal.SIGTERM,stop_signal)
    if hasattr(signal,'SIGHUP'):signal.signal(signal.SIGHUP,stop_signal)
    with simulator.console_log.open('wb') as log:
        try:
            options={'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP} if os.name=='nt' else {'start_new_session':True}
            process=subprocess.Popen(command,stdin=subprocess.PIPE,stdout=log,stderr=subprocess.STDOUT,**options)
            qmp,_=qmp_listener.accept();qmp.sendall(b'{"execute":"qmp_capabilities"}\r\n');simulator.console=process.stdin
            print(f'C1 Max 模拟器：{url}\n800×340 彩屏 · MIPS QEMU PID {process.pid} · 音频 {backend}\n数据盘：{args.disk}',flush=True)
            def monitor():
                process.wait()
                if simulator.running:server.shutdown()
            threading.Thread(target=monitor,daemon=True).start()
            if not args.no_open:webbrowser.open(url)
            server.serve_forever(poll_interval=.25)
        except KeyboardInterrupt:pass
        finally:
            if simulator.video:simulator.video.close()
            if simulator.ready and process and process.poll() is None:
                try:simulator.command('SHUTDOWN');simulator.shutdown_done.wait(5)
                except OSError:pass
            simulator.running=False
            with simulator.condition:simulator.update()
            if process and process.poll() is None:
                try:qmp.sendall(b'{"execute":"quit"}\r\n');process.wait(timeout=5)
                except (OSError,AttributeError,subprocess.TimeoutExpired):process.terminate();process.wait(timeout=5)
            if qmp:qmp.close()
            listener.close();qmp_listener.close()
            if process and process.stdin:process.stdin.close()
            if args.audio_record and args.audio_record.exists():
                with args.audio_record.open('r+b') as recording:
                    size=recording.seek(0,2);recording.seek(4);recording.write(struct.pack('<I',size-8));recording.seek(40);recording.write(struct.pack('<I',size-44))
            server.server_close()


if __name__=='__main__':main()
