#!/usr/bin/env python3
"""Linux: actual daemon/IPC/HTTP SOAP, loopback receiver, no TV or audio output."""
import http.server
import json
import os
import pathlib
import socket
import subprocess
import sys
import tempfile
import threading
import time
import xml.etree.ElementTree as ET

class Receiver(http.server.BaseHTTPRequestHandler):
    state = 'STOPPED'
    actions = []
    media = ''
    volume = 50
    def log_message(self, *args):
        pass
    def do_POST(self):
        request = self.rfile.read(int(self.headers['Content-Length']))
        root = ET.fromstring(request)
        command = list(root.find('{http://schemas.xmlsoap.org/soap/envelope/}Body'))[0]
        action = command.tag.split('}')[-1]
        cls = type(self)
        cls.actions.append(action)
        assert self.headers['SOAPAction'].endswith('#' + action + '"')
        result = ''
        if action == 'SetAVTransportURI':
            cls.media = command.find('CurrentURI').text
            metadata = ET.fromstring(command.find('CurrentURIMetaData').text)
            assert metadata.find('.//{http://purl.org/dc/elements/1.1/}title').text == '中文 & <test>'
        elif action == 'Play':
            cls.state = 'PLAYING'
        elif action == 'Pause':
            cls.state = 'PAUSED_PLAYBACK'
        elif action == 'Stop':
            cls.state = 'STOPPED'
        elif action == 'Seek':
            assert command.find('Target').text == '00:00:04'
        elif action == 'SetVolume':
            cls.volume = int(command.find('DesiredVolume').text)
        elif action == 'GetVolume':
            result = f'<CurrentVolume>{cls.volume}</CurrentVolume>'
        elif action == 'GetTransportInfo':
            result = f'<CurrentTransportState>{cls.state}</CurrentTransportState>'
        elif action == 'GetPositionInfo':
            result = '<RelTime>00:00:04</RelTime><TrackDuration>00:01:00</TrackDuration>'
        else:
            raise AssertionError(action)
        body = ('<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/">'
                f'<s:Body><Response>{result}</Response></s:Body></s:Envelope>').encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

def wait(predicate, seconds=5):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(.03)
    raise AssertionError('condition timed out')

def request(path, message):
    with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as connection:
        connection.settimeout(1)
        connection.connect(str(path))
        connection.send(json.dumps(message).encode())
        return json.loads(connection.recv(49152))

def owner(path):
    result = request(path, {'action':'load', 'owner':'other', 'url':'http://example.invalid/next',
                            'title':'中文 & <test>', 'mime':'audio/mpeg'})
    assert result['accepted']
    print(result['accepted_id'], flush=True)
    sys.stdin.readline()

if len(sys.argv) == 3 and sys.argv[1] == '--owner':
    owner(sys.argv[2])
    sys.exit()

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Receiver)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    with tempfile.TemporaryDirectory(prefix='c1cast-') as folder:
        root = pathlib.Path(folder)
        env = dict(os.environ, C1_APPS_DATA=folder, C1_CAST_TEST_PORT=str(server.server_port))
        daemon = subprocess.Popen([sys.argv[1], '--daemon'], env=env)
        child = None
        path = root / 'cast/control.sock'
        try:
            wait(path.exists)
            assert path.stat().st_mode & 0o777 == 0o600
            call = lambda message: request(path, message)
            status = lambda: call({'action':'status'})
            assert not call({'action':[]})['ok']
            assert not call({'action':'bogus'})['ok']
            call({'action':'scan'})
            wait(lambda: len(status()['devices']) == 1)
            call({'action':'select','id':'dlna:fixture'})
            wait(lambda: status()['connected'])
            loaded = call({'action':'load','owner':'test','url':'http://example.invalid/a?key=a&b=1',
                           'title':'中文 & <test>','mime':'video/mp4','position':4})
            wait(lambda: status()['state'] == 'playing')
            assert status()['content_id'] == loaded['accepted_id']
            assert Receiver.media.endswith('?key=a&b=1')
            call({'action':'pause','owner':'test'})
            wait(lambda: status()['state'] == 'paused')
            assert status()['position'] == 4 and status()['duration'] == 60
            call({'action':'play','owner':'test'})
            wait(lambda: status()['state'] == 'playing')
            call({'action':'volume_step','owner':'global-volume','value':.05})
            wait(lambda: Receiver.volume == 55)
            call({'action':'volume_step','owner':'global-volume','value':0,'mute':True})
            wait(lambda: Receiver.volume == 0)
            # A distinct process takes ownership. The old owner cannot stop it.
            child = subprocess.Popen([sys.executable,__file__,'--owner',str(path)],
                                     stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
            next_id = int(child.stdout.readline())
            wait(lambda: status().get('content_id') == next_id and status()['state'] == 'playing')
            stops = Receiver.actions.count('Stop')
            call({'action':'stop','owner':'test'})
            time.sleep(.2)
            assert Receiver.actions.count('Stop') == stops
            child.stdin.close()
            child.wait(timeout=2)
            wait(lambda: status()['state'] == 'idle')
            assert Receiver.actions.count('Stop') > stops
            # Malformed load stays an error instead of becoming a false success
            # on the next receiver status poll; a subsequent valid load recovers.
            call({'action':'load','owner':'test','url':[]})
            wait(lambda: status()['state'] == 'error')
            time.sleep(1.2)
            assert status()['state'] == 'error'
            call({'action':'load','owner':'test','url':'http://example.invalid/a',
                  'title':'中文 & <test>','mime':'audio/mpeg'})
            wait(lambda: status()['state'] == 'playing')
            call({'action':'shutdown'})
            daemon.wait(timeout=5)
            assert daemon.returncode == 0 and not path.exists()
            assert Receiver.state == 'STOPPED'
            print('PASS Cast IPC: permissions, SOAP metadata, async controls, ownership, owner exit, errors, shutdown')
        finally:
            if child and child.poll() is None:
                child.terminate(); child.wait(timeout=3)
            if daemon.poll() is None:
                daemon.terminate(); daemon.wait(timeout=8)
finally:
    server.shutdown(); server.server_close(); thread.join()
