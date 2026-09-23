#!/usr/bin/env python3
"""Stage, checksum, atomically activate. Keeps old releases and user data."""
import argparse, pathlib, subprocess, datetime, tarfile, hashlib
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--start',action='store_true');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];payload=root/'.build/device'
if not (payload/'SHA256SUMS').exists():raise SystemExit('Run apps/tools/build.sh first')
adb=['adb','-s',a.serial]
def call(*args):subprocess.run(adb+list(args),check=True)
def shell(cmd):return subprocess.check_output(adb+['shell',cmd],text=True).replace('\r','').strip()
if shell('test -d /storage/apps/data/launcher/run.lock && echo active || true')=='active':raise SystemExit('Exit the launcher before deploying a new release')
tag=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+hashlib.sha256((payload/'SHA256SUMS').read_bytes()).hexdigest()[:8]
archive=root/'.build'/('apps-'+tag+'.tar')
with tarfile.open(archive,'w') as tar:
    for child in sorted(payload.iterdir()):tar.add(child,arcname=child.name)
base='/storage/apps';stage=base+'/releases/'+tag
call('shell','mkdir -p '+stage+' '+base+'/data/launcher '+base+'/data/streamplayer '+base+'/data/nes/roms '+base+'/data/calendar '+base+'/data/calculator '+base+'/data/terminal; chmod 700 '+base+'/data '+base+'/data/streamplayer')
call('push',str(archive),stage+'/payload.tar')
cmd=f'cd {stage} && tar xf payload.tar && sha256sum -c SHA256SUMS && rm payload.tar && chmod 755 launcher/run.sh launcher/c1max-launcher piano/c1max-piano nes/c1max-nes streamplayer/c1max-streamplayer calendar/c1max-calendar calculator/c1max-calculator terminal/c1max-terminal'
if 'OK' not in shell(cmd):raise SystemExit('Device verification failed; current release unchanged')
# old BusyBox adb does not propagate remote exit statuses, so verify explicitly.
verify=shell(f'cd {stage} && sha256sum -c SHA256SUMS >/dev/null 2>&1 && echo VERIFIED')
if verify!='VERIFIED':raise SystemExit('Checksum mismatch; current release unchanged')
print('Previous release:',shell('readlink /storage/apps/current || true'))
result=shell(f'chmod 755 {stage}/shared/c1max-activate && {stage}/shared/c1max-activate {stage}')
if result!='ACTIVATED':raise SystemExit('Activation failed: '+result)
print('Activated:',stage)
if a.start:call('shell','nohup setsid /storage/apps/current/launcher/run.sh </dev/null >/storage/apps/data/launcher/run.log 2>&1 & sleep 1')
