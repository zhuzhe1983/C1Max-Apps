#!/usr/bin/env python3
"""独立 C1 Max initramfs；原始应用 ELF 与持久数据分开。"""
import gzip
import io
import json
import pathlib
import struct

from fetch_apps import elf, DEFAULT

ROOT = pathlib.Path(__file__).resolve().parents[1]
FILES = {}


def file(path, data, mode=0o644): FILES[path.lstrip('/')] = (0o100000 | mode, data)


def cpio(target):
    entries = dict(FILES)
    for name in list(entries):
        for parent in pathlib.PurePosixPath(name).parents:
            if str(parent) != '.': entries.setdefault(str(parent), (0o40755, b''))
    for name in ('dev','proc','sys','tmp','run','root','storage','usr/bin','usr/sbin','etc'):
        entries.setdefault(name,(0o40755,b''))
    output = io.BytesIO()
    def emit(name, mode, data, inode):
        encoded=name.encode()+b'\0'
        fields=[inode,mode,0,0,2 if mode&0o170000==0o40000 else 1,0,len(data),0,0,0,0,len(encoded),0]
        if name=='dev/console':fields[9:11]=[5,1]
        output.write(b'070701'+b''.join(f'{value:08x}'.encode() for value in fields));output.write(encoded)
        output.write(b'\0'*(-output.tell()%4));output.write(data);output.write(b'\0'*(-len(data)%4))
    for index,(name,(mode,data)) in enumerate(sorted(entries.items()),1):emit(name,mode,data,index)
    emit('TRAILER!!!',0,b'',len(entries)+1)
    temporary=target.with_suffix('.part')
    with temporary.open('wb') as stream:
        with gzip.GzipFile(fileobj=stream,mode='wb',mtime=0) as archive:archive.write(output.getvalue())
    temporary.replace(target)


def main():
    FILES.clear()
    catalog=json.loads((ROOT/'catalog.json').read_text())
    upstream=json.loads((ROOT/'upstream.json').read_text())
    source=ROOT/'.cache/upstream/C1Max-Apps'
    payload=ROOT/'build/payload'
    audits=[]
    for path in sorted(payload.rglob('*')):
        if path.relative_to(payload).parts[0] not in (*DEFAULT,'launcher','shared'):continue
        if path.is_symlink():raise ValueError('资源目录不能含符号链接')
        if path.is_file():file('/opt/c1max/'+str(path.relative_to(payload)),path.read_bytes(),0o755 if path.stat().st_mode&0o111 else 0o644)
    for row in catalog['apps']:
        audit=payload/row['id']/'c1sim-audit.json'
        if row['id'] in DEFAULT and audit.is_file():audits.append(json.loads(audit.read_text()))
    for ident in ('launcher','shared'):
        for path in (payload/ident).glob('c1max-*'):
            audits.append({'id':path.name,'source_type':'upstream-source-build','revision':upstream['revision'],
                           'entry':str(path.relative_to(payload)),**elf(path.read_bytes())})
    file('/opt/c1max/shared/NotoSansSC-Regular.ttf',(source/'shared/fonts/NotoSansSC-Regular.ttf').read_bytes())
    file('/opt/c1max/shared/ca-certificates.crt',(ROOT/'build/images/ca-certificates.crt').read_bytes())
    file('/opt/c1max/shared/licenses/NotoSansSC-OFL.txt',(source/'shared/fonts/OFL.txt').read_bytes())
    file('/opt/c1max/launcher/licenses/stb_truetype.txt',(source/'launcher/licenses/stb_truetype.txt').read_bytes())
    for path in (ROOT/'build/licenses').rglob('*'):
        if path.is_file():file('/usr/share/licenses/'+str(path.relative_to(ROOT/'build/licenses')),path.read_bytes())
    apps=[{'id':'launcher','name':'C1 Max 桌面','version':'source'}]
    menu=[]
    for row in catalog['apps']:
        installed=row['id'] in DEFAULT and (payload/row['id']/('c1max-'+row['id'])).is_file()
        apps.append({'id':row['id'],'name':row['title'],'version':row['version'],'installed':installed})
        if installed:
            menu.append(row['title']+'|/storage/apps/current/'+row['id']+'/c1max-'+row['id']+'|cwd=/storage/apps/current/'+row['id']+'|icon='+('updates' if row['id']=='appstore' else row['id']))
            icon=payload/row['id']/'icon.bgra'
            if icon.is_file():file('/opt/c1max/launcher/icons/'+('updates' if row['id']=='appstore' else row['id'])+'.bgra',icon.read_bytes())
    file('/opt/c1max/launcher/apps.txt',('\n'.join(menu)+'\n').encode())
    file('/opt/c1max/catalog.json',json.dumps(catalog).encode())
    file('/etc/c1max-apps',(' '.join(row['id'] for row in apps)+' shared\n').encode())
    file('/etc/passwd',b'root:x:0:0:root:/root:/bin/sh\n');file('/etc/group',b'root:x:0:\n')
    file('/etc/hosts',b'127.0.0.1 localhost\n')
    for name,destination in [('busybox','/bin/busybox'),('c1maxsim.ko','/lib/modules/c1maxsim.ko'),('c1max-bridge','/sbin/c1max-bridge'),
                             ('c1sim-audio','/sbin/c1sim-audio'),('ffplay','/usr/bin/ffplay'),('c1max-video','/usr/bin/c1max-video'),
                             ('c1max-host-video','/usr/bin/c1max-host-video')]:
        data=(ROOT/'build/images'/name).read_bytes()
        if not name.endswith('.ko'):elf(data)
        file(destination,data,0o755)
    for optional in ('c1max-wget','c1max-mplayer'):
        path=ROOT/'build/images'/optional
        if path.exists():file('/usr/bin/'+optional.removeprefix('c1max-'),path.read_bytes(),0o755)
    for applet in ('sh ash cat cp chmod chown date df du echo env false head id ln ls mkdir mknod mv pwd readlink realpath rm rmdir sha256sum sleep sort sync tail touch true uname wc whoami yes awk find grep sed xargs kill killall ps top free dmesg mount umount insmod lsmod halt poweroff reboot setsid cttyhack stty ifconfig route ip netstat nc wget tar gunzip gzip clear flock pidof mkfifo tee tr cut').split():
        FILES['bin/'+applet]=(0o120777,b'busybox')
    file('/etc/ssl/certs/ca-certificates.crt',(ROOT/'build/images/ca-certificates.crt').read_bytes())
    for name,destination in [('init','/init'),('services.sh','/sbin/services.sh')]:file(destination,(ROOT/'guest'/name).read_bytes(),0o755)
    FILES['dev/console']=(0o20600,b'')
    cpio(ROOT/'build/images/rootfs.cpio.gz')
    (ROOT/'build/binaries.json').write_text(json.dumps(audits,ensure_ascii=False,indent=2)+'\n')
    (ROOT/'build/apps.json').write_text(json.dumps(apps,ensure_ascii=False,indent=2)+'\n')
    print('C1 Max 客体已打包；校验 '+str(len(audits))+' 个 MIPS 程序。')


if __name__=='__main__':main()
