#!/bin/sh
set -eu
exec python3 - "$0" <<'PY'
import os, pathlib, shlex, subprocess, sys, tempfile
root = pathlib.Path(sys.argv[1]).resolve().parent.parent
cc = shlex.split(os.environ.get('CC', 'cc'))
cxx = shlex.split(os.environ.get('CXX', 'c++'))
sanitize = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'] if os.environ.get('SANITIZE', '1') == '1' else []
includes = ['-I' + str(root/'src'), '-I' + str(root/'vendor/libvterm/include')]
with tempfile.TemporaryDirectory(prefix='c1max-terminal-test-') as tmp:
    build = pathlib.Path(tmp)
    objects = []
    for source in sorted((root/'vendor/libvterm/src').glob('*.c')):
        obj = build/(source.stem+'.o')
        subprocess.run(cc + ['-std=c99', '-O1', '-g'] + sanitize + includes + ['-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    common = cxx + ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-pedantic', '-O1', '-g'] + sanitize + includes
    parser = build/'terminal_test'
    subprocess.run(common + [str(root/'tests/terminal_test.cpp'), str(root/'src/terminal.cpp')] + objects + ['-o', str(parser)], check=True)
    subprocess.run([str(parser)], check=True)
    pty = build/'pty_test'
    subprocess.run(common + [str(root/'tests/pty_test.cpp'), str(root/'src/pty.cpp'), '-o', str(pty)], check=True)
    subprocess.run([str(pty)], check=True)
PY
