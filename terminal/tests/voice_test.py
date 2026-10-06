#!/usr/bin/env python3
import base64, io, json, pathlib, subprocess, threading, wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

root = pathlib.Path(__file__).resolve().parents[2]

class Fixture(BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        request = json.loads(body)
        assert request['protocol'] == 'c1max-terminal-voice/v1'
        assert request['model'] == 'test-model'
        assert request['audio']['format'] == 'wav'
        assert request['audio']['sample_rate'] == 16000
        with wave.open(io.BytesIO(base64.b64decode(request['audio']['data_base64']))) as wav:
            assert wav.getframerate() == 16000 and wav.getnchannels() == 1 and wav.getnframes() == 1600
        if self.path == '/private': assert request['context'] == {}
        else:
            assert request['context']['screen'] == 'prompt> git status'
            assert request['context']['rows'] == 14 and request['context']['columns'] == 80
        values = {'/insert': {'insert_text': 'git status --short'}, '/empty': {'text': ''},
                  '/newline': {'text': 'command\n'}, '/escape': {'text': '\x1b'},
                  '/oversize': {'text': 'x'*1201}, '/preview': {'partial_text': ''}}
        response = b'invalid JSON' if self.path == '/badjson' else json.dumps(values.get(self.path, {'text': 'git status --short'})).encode()
        if self.path == '/preview': assert request['phase'] == 'preview'
        self.send_response(500 if self.path == '/error' else 200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(response)))
        self.end_headers()
        try: self.wfile.write(response)
        except (BrokenPipeError, ConnectionResetError): pass

server = ThreadingHTTPServer(('127.0.0.1', 0), Fixture)
thread = threading.Thread(target=server.serve_forever, daemon=True)
thread.start()
try:
    out = root / '.build/terminal-tests'
    out.mkdir(parents=True, exist_ok=True)
    binary = out / 'voice-test'
    subprocess.run([
        'c++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined',
        '-fno-sanitize-recover=all', '-pthread', '-Ishared', '-I.build/include',
        'terminal/tests/voice_test.cpp', 'terminal/src/voice.cpp', 'shared/net.cpp',
        '-o', str(binary)
    ], cwd=root, check=True)
    subprocess.run([str(binary), f'http://127.0.0.1:{server.server_port}'], cwd=root, check=True)
finally:
    server.shutdown(); server.server_close(); thread.join()
