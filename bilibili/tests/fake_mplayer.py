#!/usr/bin/env python3
# Installed only in a disposable test container as /usr/bin/mplayer.
import os,select,sys,time
from pathlib import Path
Path(os.environ['C1_APPS_DATA'],'args').write_text('\n'+'\n'.join(sys.argv[1:])+'\n')
fifo=os.open(os.environ['C1_YUV_FIFO'],os.O_WRONLY)
os.write(fifo,b'YUV4MPEG2 W2 H2 F30:1 Ip A1:1 C420jpeg\n')
position=0.0;paused=False;last=time.monotonic();print('ID_LENGTH=100',flush=True)
def frame():os.write(fifo,b'FRAME\n'+bytes([100,100,100,100,128,128]))
while True:
    now=time.monotonic()
    if not paused and now-last>=.03:position+=now-last;last=now;frame()
    ready,_,_=select.select([sys.stdin],[],[],.01)
    if ready:
        line=sys.stdin.readline()
        if not line:break
        line=line.replace('pausing_keep_force ','').replace('pausing_keep ','').strip()
        if line=='quit':break
        if line=='pause':paused=not paused;last=now
        if line=='get_time_pos':print('ANS_TIME_POSITION='+str(position),flush=True)
        if line.startswith('seek '):position=float(line.split()[1]);frame();print('ANS_TIME_POSITION='+str(position),flush=True)
os.close(fifo)
