#!/usr/bin/env python3
"""Serve only one generated test video; supports Cast CORS and Range requests.

Usage: serve_fixture.py /path/to/cast-test.mp4 18764
Terminate the task-owned process after device testing.
"""
import http.server
import pathlib
import re
import sys

video = pathlib.Path(sys.argv[1]).resolve()

class Handler(http.server.BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Headers', 'Range')
        self.end_headers()

    def do_HEAD(self):
        self.serve(False)

    def do_GET(self):
        self.serve(True)

    def serve(self, body):
        if self.path != '/cast-test.mp4':
            self.send_error(404)
            return
        size = video.stat().st_size
        start, end = 0, size - 1
        value = self.headers.get('Range')
        if value:
            match = re.fullmatch(r'bytes=(\d+)-(\d*)', value)
            if not match:
                self.send_error(416)
                return
            start = int(match[1])
            end = min(end, int(match[2])) if match[2] else end
            if start > end:
                self.send_error(416)
                return
        self.send_response(206 if value else 200)
        self.send_header('Content-Type', 'video/mp4')
        self.send_header('Content-Length', str(end - start + 1))
        self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Access-Control-Allow-Origin', '*')
        if value:
            self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.end_headers()
        if body:
            with video.open('rb') as source:
                source.seek(start)
                remaining = end - start + 1
                try:
                    while remaining:
                        chunk = source.read(min(remaining, 65536))
                        if not chunk:
                            break
                        self.wfile.write(chunk)
                        remaining -= len(chunk)
                except (ConnectionResetError, BrokenPipeError):
                    pass

if __name__ == '__main__':
    with http.server.ThreadingHTTPServer(('', int(sys.argv[2])), Handler) as server:
        server.serve_forever()
