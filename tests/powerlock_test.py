#!/usr/bin/env python3
"""Exercise the vendor PowerLock wire contract, including failed acknowledgements."""
import pathlib
import signal
import socket
import subprocess
import tempfile
import time

root = pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="c1-powerlock-") as folder:
    source = pathlib.Path(folder) / "client.c"
    binary = pathlib.Path(folder) / "client"
    source.write_text('''
#include <stdlib.h>
#include <signal.h>
#include "power_lock.h"
int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    for (int i=2; i<argc; ++i)
        if (powerlock_command(atoi(argv[1]), argv[i])) return 1;
    return 0;
}
''')
    subprocess.run(["cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "shared"), str(source), "-o", str(binary)], check=True)

    def run_case(response, expected, commands=("suslock",), fragmented=False):
        peer, child = socket.socketpair()
        process = subprocess.Popen([str(binary), str(child.fileno()), *commands], pass_fds=(child.fileno(),))
        child.close()
        try:
            peer.settimeout(2)
            for command in commands:
                frame = bytearray()
                while not frame.endswith(b"\0"):
                    chunk = peer.recv(1)
                    assert chunk, "client closed before a complete command"
                    frame.extend(chunk)
                assert frame == f"Register {command} {process.pid}".encode() + b"\0", frame
                if response is None:
                    peer.close()
                elif fragmented:
                    for byte in response:
                        peer.sendall(bytes([byte]))
                        time.sleep(0.02)
                else:
                    peer.sendall(response)
            assert process.wait(timeout=3) == expected
        finally:
            peer.close()
            if process.poll() is None:
                process.kill()
            process.wait()

    run_case(b"ok\0", 0, commands=("suslock", "susunlock"))
    run_case(b"ok\0", 0, fragmented=True)
    run_case(b"failed\0", 1)
    run_case(b"ok", 1)  # A partial frame is not an acknowledgement.
    run_case(b"", 1)  # A server that ignores the command must time out.
    run_case(None, 1)
    # A blocked write must honor the same deadline as a missing reply.
    peer, child = socket.socketpair()
    child.setblocking(False)
    try:
        while True: child.send(b'x' * 8192)
    except BlockingIOError:
        pass
    child.setblocking(True)
    process = subprocess.Popen([str(binary), str(child.fileno()), "suslock"], pass_fds=(child.fileno(),))
    child.close()
    try:
        assert process.wait(timeout=3) == 1
    finally:
        peer.close()
        if process.poll() is None: process.kill()
        process.wait()
print("PASS PowerLock: NUL frames, unique PID, lock/unlock ACK, fragmented reply, rejection, timeout and disconnect")
