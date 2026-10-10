#!/usr/bin/env python3
"""Actual FFmpeg encode + HTTP capability, hostile requests and lifecycle."""
import contextlib
import importlib.util
import json
import math
import os
from pathlib import Path
import secrets
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request

SPEC = importlib.util.spec_from_file_location("game_server", Path(__file__).parents[1] / "server.py")
server = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(server)


class BridgeTest(unittest.TestCase):
    def setUp(self):
        ffmpeg = shutil.which("ffmpeg")
        if not ffmpeg:
            self.skipTest("ffmpeg unavailable")
        self.config = {"host": "127.0.0.1", "token": secrets.token_hex(32), "http_port": 0}
        self.bridge = server.Bridge(self.config, ffmpeg)
        self.control = server.ControlServer(("127.0.0.1", 0), server.ControlHandler)
        self.media = server.MediaServer(("127.0.0.1", 0), server.MediaHandler)
        self.config["http_port"] = self.media.server_address[1]
        self.control.bridge = self.media.bridge = self.bridge
        self.threads = [threading.Thread(target=item.serve_forever, daemon=True) for item in (self.control, self.media)]
        for thread in self.threads:
            thread.start()

    def tearDown(self):
        for item in (self.control, self.media):
            item.shutdown()
            item.server_close()
        if self.bridge.session:
            self.bridge.end(self.bridge.session)
        for thread in self.threads:
            thread.join(timeout=3)

    def connect(self, **overrides):
        sock = socket.create_connection(self.control.server_address, timeout=3)
        hello = {"protocol": server.PROTOCOL, "token": self.config["token"], "app": "nes", "title": "Test pattern",
                 "width": 320, "height": 240, "fps": 20, "format": "rgb565le", "channels": 2, "sample_rate": 44100}
        hello.update(overrides)
        sock.sendall(json.dumps(hello).encode() + b"\n")
        stream = sock.makefile("rb")
        return sock, stream, json.loads(stream.readline())

    def test_auth_format_and_http_are_closed(self):
        for bad in ({"token": "0" * 64}, {"width": 8192}, {"app": "../../etc"}, {"token": []}):
            sock, stream, result = self.connect(**bad)
            self.assertFalse(result["ok"])
            stream.close()
            sock.close()
            self.assertIsNone(self.bridge.session)
        for path in ("/", "/etc/passwd", "/session/" + "0" * 48 + "/../config.json"):
            with self.assertRaises(urllib.error.HTTPError) as raised:
                urllib.request.urlopen(f'http://127.0.0.1:{self.config["http_port"]}{path}', timeout=2)
            self.assertEqual(raised.exception.code, 404)
            raised.exception.close()

    def test_real_hls_and_bounded_lifecycle(self):
        sock, stream, result = self.connect()
        self.assertTrue(result["ok"])
        session = self.bridge.session
        self.assertIsNotNone(session)
        directory = session.directory
        self.assertEqual(directory.stat().st_mode & 0o077, 0)
        # A second authenticated publisher may not steal the first stream.
        other, other_stream, rejected = self.connect(app="dosbox")
        self.assertFalse(rejected["ok"])
        other_stream.close()
        other.close()
        frames = bytes.fromhex("00f8") * (320 * 240)
        samples = b"".join(struct.pack("<hh", int(math.sin(n * math.tau * 440 / 44100) * 5000), 0) for n in range(2205))
        received = []
        def read_replies():
            try:
                for line in stream:
                    received.append(json.loads(line))
            except (OSError, ValueError):
                pass
        reader = threading.Thread(target=read_replies, daemon=True)
        reader.start()
        try:
            started = time.monotonic()
            for n in range(120):
                for kind, payload in ((b"V", frames), (b"A", samples)):
                    sock.sendall(kind + b"\0\0\0" + struct.pack("!I", len(payload)) + payload)
                time.sleep(max(0, started + (n + 1) / 20 - time.monotonic()))
                if n > 50 and any(item.get("ready") for item in received):
                    break
            self.assertTrue(session.healthy(), "ffmpeg ended unexpectedly")
            self.assertLessEqual(len(session.audio), server.AUDIO_LIMIT)
            ready = [item for item in received if item.get("ready")]
            self.assertTrue(ready, "No encoded HLS playlist within 6s")
            url = ready[-1]["url"]
            playlist = urllib.request.urlopen(url, timeout=2).read().decode()
            self.assertIn("#EXTM3U", playlist)
            self.assertIn("#EXT-X-INDEPENDENT-SEGMENTS", playlist)
            segment = next(line for line in playlist.splitlines() if line.endswith(".ts"))
            segment_url = url.rsplit("/", 1)[0] + "/" + segment
            data = urllib.request.urlopen(segment_url, timeout=2).read()
            self.assertEqual(data[0], 0x47)
            ranged = urllib.request.Request(segment_url, headers={"Range": "bytes=0-187"})
            with urllib.request.urlopen(ranged, timeout=2) as response:
                self.assertEqual(response.status, 206)
                self.assertEqual(len(response.read()), 188)
            invalid = url.replace(session.key, "0" * 48)
            with self.assertRaises(urllib.error.HTTPError) as raised:
                urllib.request.urlopen(invalid, timeout=2)
            raised.exception.close()
            ffprobe = shutil.which("ffprobe")
            if ffprobe:
                import subprocess
                info = json.loads(subprocess.check_output([ffprobe, "-v", "error", "-show_streams", "-of", "json", str(directory / segment)]))
                self.assertEqual({item["codec_name"] for item in info["streams"]}, {"h264", "aac"})
                video = next(item for item in info["streams"] if item["codec_type"] == "video")
                self.assertEqual((video["width"], video["height"]), (320, 240))
                self.assertEqual(video["display_aspect_ratio"], "4:3")
            encoder = session.process
        finally:
            sock.shutdown(socket.SHUT_RDWR)
            sock.close()
            reader.join(timeout=3)
            stream.close()
        limit = time.monotonic() + 4
        while directory.exists() and time.monotonic() < limit:
            time.sleep(0.05)
        self.assertFalse(directory.exists(), "Temporary media must disappear on disconnect")
        self.assertIsNotNone(encoder.poll(), "Only this session's encoder must be reaped")

    def test_sigterm_reaps_owned_encoder(self):
        def free_port():
            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                return sock.getsockname()[1]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "config.json"
            config = {"host": "127.0.0.1", "port": free_port(), "http_port": free_port(), "token": secrets.token_hex(32)}
            path.write_text(json.dumps(config))
            path.chmod(0o600)
            process = subprocess.Popen([sys.executable, str(Path(__file__).parents[1] / "server.py"), "--config", str(path), "--bind", "127.0.0.1"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            sock = stream = None
            try:
                import select
                self.assertTrue(select.select([process.stdout], [], [], 5)[0])
                self.assertIn(b"bridge ready", process.stdout.readline())
                sock = socket.create_connection(("127.0.0.1", config["port"]), timeout=3)
                hello = {"protocol": server.PROTOCOL, "token": config["token"], "app": "nes", "title": "Signal test", "width": 320, "height": 240, "fps": 20, "format": "rgb565le", "sample_rate": 44100, "channels": 2}
                sock.sendall(json.dumps(hello).encode() + b"\n")
                stream = sock.makefile("rb")
                self.assertTrue(json.loads(stream.readline())["ok"])
                processes = subprocess.check_output(["ps", "-axo", "pid=,ppid=,comm="], text=True)
                children = [int(line.split()[0]) for line in processes.splitlines() if len(line.split()) >= 3 and int(line.split()[1]) == process.pid]
                self.assertTrue(children, "Expected a task-owned ffmpeg process")
                process.terminate()
                process.wait(timeout=7)
                self.assertEqual(process.returncode, 0)
                for child in children:
                    with self.assertRaises(ProcessLookupError):
                        os.kill(child, 0)
            finally:
                if sock is not None:
                    sock.close()
                if stream is not None:
                    stream.close()
                if process.poll() is None:
                    process.kill()
                    process.wait(timeout=3)
                process.stdout.close()
                process.stderr.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
