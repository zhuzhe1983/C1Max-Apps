#!/usr/bin/env python3
"""真实本机 FFmpeg、Range 转发、暂停跳转、自然结束和子进程清理回归。"""
import contextlib
import http.server
import pathlib
import secrets
import socket
import subprocess
import sys
import threading
import time
import types
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]; sys.path.insert(0, str(ROOT))
import start
from video import VideoService


class VideoTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture = ROOT/'build/debug/bilibili/service-fixture.mp4'; cls.fixture.parent.mkdir(parents=True, exist_ok=True)
        if not cls.fixture.exists():
            subprocess.run(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=128x72:rate=10:duration=3',
                            '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000:duration=3', '-c:v', 'libx264', '-preset', 'ultrafast',
                            '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-ac', '2', '-movflags', '+faststart', '-shortest', str(cls.fixture)], check=True)

    def setUp(self):
        self.token = secrets.token_hex(16)
        sim = types.SimpleNamespace(token=self.token, video=None)
        self.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), start.handler(sim, 0, None)); self.server.daemon_threads = True
        self.service = sim.video = VideoService(self.token, self.server.server_address[1])
        threading.Thread(target=self.server.serve_forever, daemon=True).start()

    def tearDown(self):
        self.service.close(); self.server.shutdown(); self.server.server_close()

    @contextlib.contextmanager
    def client(self, source='/tmp/fixture.mp4', headers='', upload=True):
        body = self.fixture.read_bytes() if upload else b''
        connection = socket.create_connection(self.server.server_address); connection.settimeout(10)
        stream = connection.makefile('rb')
        request = f'POST /video HTTP/1.1\r\nHost: localhost\r\nX-C1Sim-Token: {self.token}\r\nX-C1Max-URL: {source}\r\nContent-Length: {len(body)}\r\n{headers}\r\n'
        connection.sendall(request.encode()+body)
        self.assertIn(b'101', stream.readline())
        while stream.readline() != b'\r\n': pass
        def receive():
            line = stream.readline().decode().strip(); self.assertTrue(line, '加速服务提前断开')
            fields = line.split(); data = stream.read(int(fields[2])) if fields[0] in ('V', 'A') else b''
            if fields[0] in ('V', 'A'): self.assertEqual(len(data), int(fields[2]))
            return fields, data
        try: yield connection, receive
        finally:
            stream.close(); connection.close()

    def until(self, receive, kind):
        while True:
            fields, data = receive(); self.assertNotEqual(fields[0], 'X', fields)
            if fields[0] == kind: return fields, data

    def test_pause_exact_seek_resume(self):
        with self.client() as (connection, receive):
            self.until(receive, 'V'); connection.sendall(b'pause\n'); self.until(receive, 'S')
            connection.sendall(b'seek 1.343 2\n'); self.until(receive, 'S')
            fields, data = self.until(receive, 'V'); self.assertAlmostEqual(float(fields[1]), 1.343, delta=.11)
            self.assertEqual(len(data), 128*72*3//2)
            connection.sendall(b'pausing_keep_force get_time_pos\n'); fields, _ = self.until(receive, 'T')
            self.assertAlmostEqual(float(fields[1]), 1.343, delta=.01)
            connection.sendall(b'pause\n'); self.until(receive, 'S'); self.until(receive, 'V')
            connection.sendall(b'seek 0.343 2\n'); self.until(receive, 'S'); self.until(receive, 'V')
            fields, _ = self.until(receive, 'E'); self.assertEqual(fields, ['E', '0'])
        deadline = time.monotonic()+3
        while self.service.sessions and time.monotonic() < deadline: time.sleep(.05)
        self.assertFalse(self.service.sessions)

    def test_stop_cancels_decoder_and_removes_upload(self):
        with self.client(headers='X-C1Max-Silent: 1\r\n') as (_, receive):
            fields, _ = self.until(receive, 'M'); self.assertEqual(fields[-1], '0')
            self.until(receive, 'V')
            with self.service.lock: session = next(iter(self.service.sessions.values()))
            process, upload = session.process, session.upload
            self.service.close()
            self.assertIsNotNone(process.poll())
        deadline = time.monotonic()+3
        while self.service.sessions and time.monotonic() < deadline: time.sleep(.05)
        self.assertFalse(self.service.sessions); self.assertFalse(upload.exists())

    def test_network_headers_range_and_eof(self):
        requests = []; fixture = self.fixture.read_bytes()
        class Source(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_): pass
            def do_GET(self):
                allowed = self.headers.get('Referer') == 'https://www.bilibili.com/' and self.headers.get('User-Agent') == 'Mozilla/5.0'
                requests.append({'allowed': allowed, 'range': bool(self.headers.get('Range'))})
                if not allowed: self.send_error(403); return
                begin, end = 0, len(fixture)-1
                if self.headers.get('Range'):
                    first, last = self.headers['Range'].removeprefix('bytes=').split('-'); begin = int(first); end = int(last) if last else end
                data = fixture[begin:end+1]; self.send_response(206 if self.headers.get('Range') else 200)
                self.send_header('Content-Length', str(len(data))); self.send_header('Accept-Ranges', 'bytes')
                if self.headers.get('Range'): self.send_header('Content-Range', f'bytes {begin}-{end}/{len(fixture)}')
                self.end_headers()
                try: self.wfile.write(data)
                except OSError: pass
        source = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Source)
        threading.Thread(target=source.serve_forever, daemon=True).start()
        try:
            address = f'http://10.0.2.2:{source.server_address[1]}/clip.mp4'
            with self.client(address, 'Referer: https://www.bilibili.com/\r\nUser-Agent: Mozilla/5.0\r\n', False) as (_, receive):
                frames = audio = 0
                while True:
                    fields, data = receive(); self.assertNotEqual(fields[0], 'X', fields)
                    if fields[0] == 'V': frames += 1
                    elif fields[0] == 'A': audio += len(data)
                    elif fields[0] == 'E': self.assertEqual(fields, ['E', '0']); break
                self.assertEqual(frames, 30); self.assertEqual(audio, 3*192000)
            self.assertTrue(requests and all(r['allowed'] for r in requests)); self.assertTrue(any(r['range'] for r in requests))
            with self.client(address, '', False) as (_, receive):
                fields, _ = receive(); self.assertEqual(fields, ['X', 'http_403'])
        finally: source.shutdown(); source.server_close()

if __name__ == '__main__': unittest.main()
