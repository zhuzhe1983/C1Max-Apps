#!/usr/bin/env python3
"""Actual game lock lifecycle against a local vendor-protocol fixture."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="c1-awake-") as folder:
    path = Path(folder)
    server = socket.socket(socket.AF_UNIX)
    server.bind(str(path / 'power'))
    server.listen(2)
    server.settimeout(3)
    source = path / 'client.c'
    source.write_text(r'''#include <assert.h>
#include <signal.h>
#include "game_awake.h"
int main(void) {
    signal(SIGPIPE,SIG_IGN);
    assert(c1_game_needs_awake("nes"));
    assert(c1_game_needs_awake("pcsx4all"));
    assert(c1_game_needs_awake("dosbox"));
    assert(!c1_game_needs_awake("terminal"));
    assert(!c1_game_needs_awake(NULL));
    C1GameAwake g={.fd=-1};
    c1_game_awake_tick(&g,0);assert(g.fd>=0);
    c1_game_awake_tick(&g,1000); /* Must not issue redundant commands. */
    c1_game_awake_tick(&g,5000);assert(g.fd<0); /* Lost second ACK. */
    c1_game_awake_tick(&g,10000);assert(g.fd>=0);
    c1_game_awake_end(&g);assert(g.fd<0);
    c1_game_awake_end(&g); /* Idempotent cleanup. */
    return 0;
}
''')
    subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror',
                    '-DC1_POWERLOCK_PATH="'+str(path/'power')+'"', '-I',str(root/'shared'),
                    str(source),'-o',str(path/'client')],check=True)
    child=subprocess.Popen([str(path/'client')])
    def command(peer, expected, response=b'ok\0'):
        peer.settimeout(3)
        frame=bytearray()
        while not frame.endswith(b'\0'):
            chunk=peer.recv(1)
            assert chunk
            frame.extend(chunk)
        assert frame==f'Register {expected} {child.pid}'.encode()+b'\0',frame
        peer.sendall(response)
    try:
        peer,_=server.accept()
        with peer:
            for key in ('suslock','bllock','suslock'):command(peer,key)
            command(peer,'bllock',b'failed\0')
            assert peer.recv(1)==b'' # Partial acquisition is released by close.
        peer,_=server.accept()
        with peer:
            for key in ('suslock','bllock','blunlock','susunlock'):command(peer,key)
            assert peer.recv(1)==b''
        assert child.wait(timeout=3)==0
    finally:
        if child.poll() is None:child.kill()
        child.wait()
        server.close()
print('PASS game locks: scope, throttling, rejected ACK, reconnect, release and idempotent cleanup')
