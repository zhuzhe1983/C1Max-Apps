#!/usr/bin/env python3
"""Exercise the native Cast sender against a local TLS Cast V2 fixture.

Checks LOAD metadata, controls, framing, TLS certificate pinning and rejection;
does not assert physical TV decode support.
"""
import json
import pathlib
import socketserver
import ssl
import struct
import subprocess
import sys
import tempfile
import threading

def varint(value):
    out = bytearray()
    while value > 127:
        out.append((value & 127) | 128)
        value >>= 7
    out.append(value)
    return out

def encode(source, destination, namespace, payload):
    out = bytearray(b'\x08\x00')
    for field, value in ((2, source),(3, destination),(4, namespace),(6,json.dumps(payload))):
        data = value.encode()
        out += varint(field * 8 + 2) + varint(len(data)) + data
    return struct.pack('!I', len(out)) + out

def decode(data):
    at = 0
    def integer():
        nonlocal at
        value, shift = 0, 0
        while True:
            byte = data[at]; at += 1
            value |= (byte & 127) << shift
            if byte < 128:
                return value
            shift += 7
    fields = {}
    while at < len(data):
        tag = integer()
        if tag & 7 == 0:
            integer()
        else:
            assert tag & 7 == 2
            length = integer()
            fields[tag >> 3] = data[at:at+length].decode()
            at += length
    return fields, json.loads(fields[6])

class Cast(socketserver.BaseRequestHandler):
    actions = []
    errors = []
    launch_error = False
    def handle(self):
        state, active, loaded = 'IDLE', False, False
        try:
            with self.server.context.wrap_socket(self.request, server_side=True) as stream:
                stream.settimeout(5)
                def receive(n):
                    out = bytearray()
                    while len(out) < n:
                        chunk = stream.recv(n-len(out))
                        if not chunk:
                            raise EOFError()
                        out += chunk
                    return out
                while True:
                    fields, payload = decode(receive(struct.unpack('!I', receive(4))[0]))
                    namespace, action = fields[4], payload['type']
                    type(self).actions.append(action)
                    result = None
                    source = 'receiver-0'
                    if namespace.endswith('.receiver'):
                        if action == 'LAUNCH':
                            assert payload['appId'] == 'CC1AD845'
                            if type(self).launch_error:
                                result = {'type':'LAUNCH_ERROR','reason':'NOT_FOUND'}
                            else:
                                active = True
                        if action == 'STOP':
                            active, loaded = False, False
                        if result is None:
                            applications = [{'appId':'CC1AD845','transportId':'transport-1','sessionId':'session-1'}] if active else []
                            result = {'type':'RECEIVER_STATUS','status':{'applications':applications,'volume':{'level':.5}}}
                    elif namespace.endswith('.media'):
                        source = 'transport-1'
                        if action == 'LOAD':
                            assert payload['media']['metadata']['title'] == '中文 & <test>'
                            assert payload['media']['contentId'] == 'http://example.invalid/test?a=1&b=2'
                            assert payload['media']['contentType'] == 'video/mp4'
                            state, loaded = 'PLAYING', True
                        elif action == 'PLAY':
                            state = 'PLAYING'
                        elif action == 'PAUSE':
                            state = 'PAUSED'
                        elif action == 'STOP':
                            state, loaded = 'IDLE', False
                        elif action == 'SEEK':
                            assert payload['currentTime'] == 4
                        result = {'type':'MEDIA_STATUS','status':[{'mediaSessionId':42,'playerState':state,'currentTime':4,'media':{'duration':60}}] if loaded else []}
                    elif namespace.endswith('.heartbeat') and action == 'PING':
                        result = {'type':'PONG'}
                    if result is not None:
                        # Fragment the transport header to exercise reassembly.
                        packet = encode(source, fields[2], namespace, result)
                        stream.sendall(packet[:2]); stream.sendall(packet[2:])
        except (EOFError, TimeoutError, ConnectionError, ssl.SSLError):
            pass
        except Exception as error:
            type(self).errors.append(repr(error))

class Server(socketserver.ThreadingTCPServer):
    daemon_threads = True

with tempfile.TemporaryDirectory(prefix='c1-cast-tls-') as folder:
    root = pathlib.Path(folder)
    key, cert = root/'key.pem', root/'cert.pem'
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-subj','/CN=Cast fixture',
                    '-keyout',str(key),'-out',str(cert),'-days','1'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert,key)
    with Server(('127.0.0.1',0),Cast) as server:
        server.context = context
        thread = threading.Thread(target=server.serve_forever,daemon=True); thread.start()
        try:
            device, media = root/'device.json',root/'media.json'
            device.write_text(json.dumps({'protocol':'cast','id':'cast:test','host':'127.0.0.1','port':server.server_address[1]}))
            media.write_text(json.dumps({'url':'http://example.invalid/test?a=1&b=2','title':'中文 & <test>','mime':'video/mp4'}))
            args = [sys.argv[1],'--exercise',str(device),folder,str(media)]
            result = subprocess.run(args,capture_output=True,text=True,timeout=20)
            assert result.returncode == 0, result.stdout + result.stderr
            assert all(action in Cast.actions for action in ('LOAD','PAUSE','PLAY','SEEK','STOP'))
            assert not Cast.errors, Cast.errors
            pin = root/'pins.json'
            assert pin.stat().st_mode & 0o777 == 0o600
            Cast.launch_error = True
            result = subprocess.run(args,capture_output=True,text=True,timeout=10)
            assert result.returncode == 1 and 'NOT_FOUND' in result.stderr
            pin.write_text(json.dumps({'cast:test':'wrong-certificate'}))
            result = subprocess.run(args,capture_output=True,text=True,timeout=10)
            assert result.returncode == 1 and '证书已改变' in result.stderr
            print('PASS native Cast TLS: fragmented framing, LOAD metadata, play/pause/seek/stop, receiver errors, certificate pinning')
        finally:
            server.shutdown(); thread.join()
