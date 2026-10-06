import base64
import copy
import importlib.util
import io
import json
from pathlib import Path
import socket
import threading
import time
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import wave

spec = importlib.util.spec_from_file_location('gateway', Path(__file__).parents[1] / 'terminal_voice_gateway.py')
gateway = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gateway)


class Upstream(BaseHTTPRequestHandler):
    text = 'git status --short'
    result = '{"text":"git status --short"}'
    asr_delay = 0
    llm_delay = 0
    calls = []

    def log_message(self, *_): pass

    def do_POST(self):
        body = self.rfile.read(int(self.headers['Content-Length']))
        self.calls.append((self.path, body, self.headers.get('Authorization')))
        if self.path == '/redirect':
            self.send_response(302)
            self.send_header('Location', '/unexpected')
            self.end_headers()
            return
        time.sleep(self.asr_delay if self.path == '/asr' else self.llm_delay)
        data = {'text': self.text} if self.path == '/asr' else {'choices': [{'message': {'content': self.result}}]}
        raw = json.dumps(data).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        try: self.wfile.write(raw)
        except (BrokenPipeError, ConnectionResetError): pass


class ProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.upstream = ThreadingHTTPServer(('127.0.0.1', 0), Upstream)
        cls.server = gateway.VoiceServer(('127.0.0.1', 0), gateway.Handler)
        cls.threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in (cls.upstream, cls.server)]
        for thread in cls.threads: thread.start()
        cls.base = f'http://127.0.0.1:{cls.upstream.server_port}'
        cls.url = f'http://127.0.0.1:{cls.server.server_port}/v1/terminal/voice'
        stream = io.BytesIO()
        with wave.open(stream, 'wb') as wav:
            wav.setnchannels(1); wav.setsampwidth(2); wav.setframerate(16000)
            wav.writeframes(b'\0' * 3200)
        cls.payload = {'protocol': 'c1max-terminal-voice/v1', 'phase': 'final',
                       'audio': {'format': 'wav', 'encoding': 'pcm_s16le', 'sample_rate': 16000,
                                 'channels': 1, 'data_base64': base64.b64encode(stream.getvalue()).decode()},
                       'context': {'screen': 'untrusted window text', 'rows': 14, 'columns': 80}}

    @classmethod
    def tearDownClass(cls):
        for server in (cls.server, cls.upstream): server.shutdown(); server.server_close()
        for thread in cls.threads: thread.join()

    def setUp(self):
        gateway.ASR_URL = self.base + '/asr'; gateway.LLM_URL = self.base + '/llm'
        gateway.LLM_MODEL = 'fixture'; gateway.read_key = lambda: 'fixture-key'
        gateway.AUTH_TOKEN = 'fixture-client'; gateway.TIMEOUT = 3
        Upstream.text = 'git status --short'; Upstream.result = '{"text":"git status --short"}'
        Upstream.asr_delay = Upstream.llm_delay = 0; Upstream.calls = []

    def request(self, data=None, token='fixture-client'):
        request = Request(self.url, json.dumps(self.payload if data is None else data).encode(),
                          {'Content-Type': 'application/json', 'Authorization': 'Bearer ' + token})
        try:
            with urlopen(request, timeout=5) as response: return response.status, json.load(response)
        except HTTPError as error:
            try: return error.code, json.load(error)
            finally: error.close()

    def test_final_and_preview(self):
        status, result = self.request()
        self.assertEqual((status, result['text']), (200, 'git status --short'))
        self.assertEqual([call[0] for call in Upstream.calls], ['/asr', '/llm'])
        messages = json.loads(Upstream.calls[-1][1])['messages']
        self.assertIn('非可信', messages[0]['content'])
        self.assertEqual(json.loads(messages[1]['content'])['window_context'], 'untrusted window text')
        data = copy.deepcopy(self.payload); data['phase'] = 'preview'; Upstream.calls = []
        status, result = self.request(data)
        self.assertEqual((status, result['partial_text']), (200, 'git status --short'))
        self.assertEqual([call[0] for call in Upstream.calls], ['/asr'])

    def test_auth_and_private_health(self):
        self.assertEqual(self.request(token='wrong')[0], 401)
        with urlopen(self.url.replace('/v1/terminal/voice', '/health')) as response:
            health = json.load(response)
        self.assertIs(health['asr'], True)
        self.assertNotIn(self.base, json.dumps(health))
        self.assertEqual(Upstream.calls, [])

    def test_invalid_request_shapes_and_wav(self):
        for data in ([], {'protocol': self.payload['protocol'], 'audio': []}):
            self.assertEqual(self.request(data)[0], 400)
        for field, value in [('data_base64', 'invalid!'), ('sample_rate', 44100), ('channels', 2)]:
            data = copy.deepcopy(self.payload); data['audio'][field] = value
            self.assertEqual(self.request(data)[0], 400)
        data = copy.deepcopy(self.payload)
        data['audio']['data_base64'] = base64.b64encode(b'RIFF' + b'\0'*4 + b'WAVE' + b'\0'*32).decode()
        self.assertEqual(self.request(data)[0], 400)
        self.assertEqual(Upstream.calls, [])

    def test_control_characters_and_polish_fallback(self):
        for text in ('echo a\n', '\x1b[31m', 'x\0y', 'x'*1201):
            Upstream.text = text
            self.assertIn(self.request()[0], (400, 502))
        Upstream.text = 'safe command'; Upstream.result = '{"text":"unsafe\\n"}'
        status, result = self.request()
        self.assertEqual((status, result['text']), (200, 'safe command'))
        self.assertIn('warning', result)

    def test_deadline_shared_by_asr_and_polish(self):
        gateway.TIMEOUT = .35; Upstream.asr_delay = .25; Upstream.llm_delay = .25
        started = time.monotonic()
        status, result = self.request()
        self.assertLess(time.monotonic() - started, .55)
        self.assertEqual((status, result['text']), (200, 'git status --short'))

    def test_redirect_does_not_forward_model_credentials(self):
        gateway.LLM_URL = self.base + '/redirect'
        status, result = self.request()
        self.assertEqual((status, result['text']), (200, 'git status --short'))
        self.assertNotIn('/unexpected', [call[0] for call in Upstream.calls])

    def test_bounded_concurrent_clients(self):
        sockets = []
        try:
            for _ in range(4):
                connection = socket.create_connection(self.server.server_address)
                connection.sendall(b'POST /v1/terminal/voice HTTP/1.0\r\n')
                sockets.append(connection)
            time.sleep(.05)
            with socket.create_connection(self.server.server_address, timeout=2) as connection:
                connection.sendall(b'GET /health HTTP/1.0\r\n\r\n')
                self.assertIn(b'503', connection.recv(1024))
        finally:
            for connection in sockets: connection.close()
            time.sleep(.05)


if __name__ == '__main__': unittest.main()
