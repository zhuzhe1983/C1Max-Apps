#!/bin/sh
# Linux only: real IPC/daemon/child processes, fake silent player, no hardware/network.
set -eu
cd "$(dirname "$0")/.."
mkdir -p .build/audio-tests
flags='-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer'
cc -std=c11 $flags -Ishared -c shared/audio_client.c -o .build/audio-tests/client.o
c++ -std=c++17 $flags -Ishared shared/audio_service.cpp .build/audio-tests/client.o -o .build/audio-tests/c1max-audio
c++ -std=c++17 $flags -Ishared tests/audio_service_test.cpp .build/audio-tests/client.o -o .build/audio-tests/test
python3 - <<'PY'
import pathlib,tempfile,subprocess,shutil,os,socket,struct,time
with tempfile.TemporaryDirectory(prefix='c1-audio-') as d:
    root=pathlib.Path(d)
    for owner in ['airtune','streamplayer','piano']:
        (root/owner).mkdir();shutil.copy2('.build/audio-tests/c1max-audio',root/owner/'c1max-audio')
    player=root/'fake-player.py'
    player.write_text('''#!/usr/bin/python3
import sys,select,time
url=sys.argv[-1]
if url.endswith('/fail'):sys.exit(1)
assert '-novideo' in sys.argv and '-vo' in sys.argv and 'null' in sys.argv
print('AO: fixture',flush=True)
start=time.monotonic()
while True:
    if url.endswith('/short') and time.monotonic()-start>.45:break
    r,_,_=select.select([sys.stdin],[],[],.02)
    if r:
        line=sys.stdin.readline()
        if not line or line.strip()=='quit':break
        if 'get_time_pos' in line:print('ANS_TIME_POSITION=0.2',flush=True)
''');player.chmod(0o755)
    try:subprocess.run(['.build/audio-tests/test',d,d],check=True,timeout=25)
    finally:
        # Failed assertions still stop only this test's daemon/player.
        path=root/'data/audio/service.sock'
        if path.exists():
            try:
                s=socket.socket(socket.AF_UNIX,socket.SOCK_SEQPACKET);s.settimeout(1);s.connect(str(path))
                packet=bytearray(2344);struct.pack_into('=Iii',packet,0,0x43314131,10,0);s.send(packet);s.close()
            except OSError:pass
        time.sleep(.3)
PY
