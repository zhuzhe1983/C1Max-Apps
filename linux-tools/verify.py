#!/usr/bin/env python3
"""Check ELF contracts and run the actual MIPS binaries under qemu-user."""
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / '.build'
RUNTIME = BUILD / 'linux-tools'
BIN = RUNTIME / 'bin'


def run(args, **kwargs):
    return subprocess.run(args, check=True, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, timeout=30, **kwargs).stdout


def mips(name, *args, **kwargs):
    return run(['qemu-mipsel', str(BIN / name), *args], **kwargs)


report = {'target': 'MIPS32r2 little-endian, o32, static glibc', 'binaries': {}, 'checks': []}
for name in ('bash', 'less', 'nano', 'dbclient', 'dropbearkey'):
    binary = BIN / name
    header = run(['mipsel-linux-gnu-readelf', '-h', str(binary)])
    program = run(['mipsel-linux-gnu-readelf', '-l', str(binary)])
    dynamic = run(['mipsel-linux-gnu-readelf', '-d', str(binary)])
    assert 'ELF32' in header and 'little endian' in header and 'MIPS' in header, header
    assert 'mips32r2' in header and 'o32' in header, header
    assert 'INTERP' not in program and '(NEEDED)' not in dynamic, name
    report['binaries'][name] = {'bytes': binary.stat().st_size,
                                'sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
report['checks'].append('All 5 executables are ELF32 little-endian MIPS32r2/o32 without PT_INTERP or DT_NEEDED')
assert 'version 5.3.20' in mips('bash', '--version')
assert 'less 710' in mips('less', '--version')
assert 'version 9.2' in mips('nano', '--version')
assert '2026.94' in mips('dbclient', '-V')
assert mips('bash', '--noprofile', '--norc', '-c',
            'a=(zero one two); [[ ${a[1]} == one ]] || exit 1; '
            'f() { local n=$1; printf "%s:%s\\n" "$n" "$((3 * 7))"; }; f ok') == 'ok:21\n'
assert mips('less', input='C1Max pager smoke test\nsecond line\n') == 'C1Max pager smoke test\nsecond line\n'
report['checks'].append('QEMU: versions, Bash arrays/functions/arithmetic, less pipe passthrough')
with tempfile.TemporaryDirectory(prefix='c1-tools-verify-', dir=BUILD) as temporary:
    key = str(Path(temporary) / 'test-ed25519')
    assert 'ssh-ed25519 ' in mips('dropbearkey', '-t', 'ed25519', '-f', key)
    assert 'ssh-ed25519 ' in mips('dropbearkey', '-y', '-f', key)
report['checks'].append('QEMU: temporary Ed25519 key generation and public-key readback; key removed')

# A loopback-only banner fixture checks the client's static NSS/files lookup and
# TCP path without credentials, host-key acceptance or a remote SSH account.
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as listener:
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(10)
    port = listener.getsockname()[1]
    child = subprocess.Popen(['qemu-mipsel', str(BIN / 'dbclient'), '-p', str(port),
                              'root@localhost'], stdin=subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        connection, address = listener.accept()
        with connection:
            connection.settimeout(10)
            connection.sendall(b'SSH-2.0-C1MaxBuildFixture\r\n')
            banner = b''
            while b'\n' not in banner and len(banner) < 512:
                data = connection.recv(512)
                if not data:
                    break
                banner += data
            assert banner.startswith(b'SSH-2.0-dropbear_2026.94'), banner
        child.communicate(timeout=10)
        assert child.returncode != 0  # Fixture intentionally stops before KEX/auth.
    finally:
        if child.poll() is None:
            child.kill()
            child.communicate()
report['checks'].append('QEMU: dbclient resolves localhost and exchanges SSH banners over isolated loopback TCP')
for term in ('xterm-256color', 'vt100'):
    assert term in run(['infocmp', '-A', str(RUNTIME / 'share/terminfo'), term])

# Exercise the cross-built terminfo reader as well as the host compiler.
source = BUILD / 'verify-terminfo.c'
source.write_text('#include <curses.h>\n#include <term.h>\n#include <stdio.h>\n'
                  'int main(int argc,char **argv) { int e=0; if(argc!=2)return 2; '
                  'if(setupterm(argv[1],1,&e)!=OK || e!=1)return 3; '
                  'char *s=tigetstr("cup"); if(!s || s==(char*)-1)return 4; '
                  'printf("%s:%d\\n",argv[1],tigetnum("colors")); return 0; }\n')
probe = BUILD / 'verify-terminfo'
run(['mipsel-linux-gnu-gcc', '-Os', '-march=mips32r2', '-mabi=32', '-static',
     '-I' + str(BUILD / 'sysroot/usr/include'), str(source),
     '-L' + str(BUILD / 'sysroot/usr/lib'), '-lncursesw', '-o', str(probe)])
env = dict(os.environ, TERMINFO=str(RUNTIME / 'share/terminfo'))
assert run(['qemu-mipsel', str(probe), 'xterm-256color'], env=env) == 'xterm-256color:256\n'
assert run(['qemu-mipsel', str(probe), 'vt100'], env=env) == 'vt100:-1\n'
report['checks'].append('QEMU: cross-built ncurses loads xterm-256color (256 colors) and vt100 with cursor addressing')
report['runtime_bytes'] = sum(p.stat().st_size for p in RUNTIME.rglob('*') if p.is_file())
report['limitations'] = ['No device ADB, framebuffer/PTY integration or remote SSH login was tested here.']
(BUILD / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
