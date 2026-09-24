#!/usr/bin/env python3
"""Stage, checksum, atomically activate. Keeps old releases and user data."""
import argparse, pathlib, subprocess, datetime, tarfile, hashlib, json, re
from local_defaults import load_defaults
p=argparse.ArgumentParser();p.add_argument('--serial',required=True);p.add_argument('--start',action='store_true');p.add_argument('--defaults',type=pathlib.Path,help='Private server defaults; omitted uses config/default-servers.local.json if present');a=p.parse_args()
root=pathlib.Path(__file__).resolve().parents[1];payload=root/'.build/device'
defaults=a.defaults or root/'config/default-servers.local.json'
if a.defaults and not defaults.is_file():raise SystemExit('Private defaults file not found')
if defaults.is_file():
    try:load_defaults(defaults)
    except ValueError as error:raise SystemExit(str(error)) from None
if not (payload/'SHA256SUMS').exists():raise SystemExit('Run apps/tools/build.sh first')
ids=[entry['id'] for entry in json.loads((payload/'catalog.json').read_text())['apps']]
if any(not re.fullmatch('[a-z][a-z0-9_-]{0,31}',name) for name in ids):raise SystemExit('Invalid app ID')
adb=['adb','-s',a.serial]
def call(*args):subprocess.run(adb+list(args),check=True)
def shell(cmd):return subprocess.check_output(adb+['shell',cmd],text=True).replace('\r','').strip()
if shell('test -d /storage/apps/data/launcher/run.lock && echo active || true')=='active':raise SystemExit('Exit the launcher before deploying a new release')
tag=datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+hashlib.sha256((payload/'SHA256SUMS').read_bytes()).hexdigest()[:8]
archive=root/'.build'/('apps-'+tag+'.tar')
with tarfile.open(archive,'w') as tar:
    for child in sorted(payload.iterdir()):tar.add(child,arcname=child.name)
base='/storage/apps';stage=base+'/releases/'+tag
folders=[base+'/data/'+name for name in ids]+[base+'/data/nes/roms',base+'/data/pcsx4all/roms',base+'/data/dosbox/games',base+'/data/crosspoint/books',base+'/data/camera/photos']
call('shell','mkdir -p '+stage+' '+' '.join(folders)+'; chmod 700 '+base+'/data '+' '.join(folders))
call('push',str(archive),stage+'/payload.tar')
executables=['launcher/run.sh','pcsx4all/c1max-psx-core','dosbox/c1max-dos-core']+[name+'/c1max-'+name for name in ids]
cmd=f'cd {stage} && tar xf payload.tar && sha256sum -c SHA256SUMS && rm payload.tar && chmod 755 '+ ' '.join(executables)
if 'OK' not in shell(cmd):raise SystemExit('Device verification failed; current release unchanged')
# old BusyBox adb does not propagate remote exit statuses, so verify explicitly.
verify=shell(f'cd {stage} && sha256sum -c SHA256SUMS >/dev/null 2>&1 && echo VERIFIED')
if verify!='VERIFIED':raise SystemExit('Checksum mismatch; current release unchanged')
print('Previous release:',shell('readlink /storage/apps/current || true'))
result=shell(f'chmod 755 {stage}/shared/c1max-activate && {stage}/shared/c1max-activate {stage}')
if result!='ACTIVATED':raise SystemExit('Activation failed: '+result)
print('Activated:',stage)
if defaults.is_file():
    # Keep personal addresses/accounts outside the verified public release tree.
    temp=base+'/data/.default-servers-'+tag+'.tmp'
    call('push',str(defaults),temp)
    result=shell(f'chmod 600 {temp} && mv {temp} {base}/data/default-servers.json && echo DEFAULTS_SAVED')
    if result!='DEFAULTS_SAVED':raise SystemExit('Release activated, but private defaults could not be saved')
    print('Private server defaults synced separately (0600); saved app settings take priority')
if a.start:call('shell','nohup setsid /storage/apps/current/launcher/run.sh </dev/null >/storage/apps/data/launcher/run.log 2>&1 & sleep 1')
