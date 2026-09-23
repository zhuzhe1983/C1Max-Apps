#!/usr/bin/env python3
"""Install/remove the independent launcher hotkey boot service, preserving ADB."""
import argparse
import datetime
import hashlib
import pathlib
import shlex
import subprocess

BEGIN = '# BEGIN C1MAX APPS HOTKEY'
END = '# END C1MAX APPS HOTKEY'
BLOCK = '''# BEGIN C1MAX APPS HOTKEY
on property:init.svc.smartUI=running
    start c1apps-hotkey

service c1apps-hotkey /storage/apps/current/shared/c1max-hotkey
    class main
    disabled
# END C1MAX APPS HOTKEY
'''

def patch(original, install):
    text = original.decode('utf-8')
    if text.count(BEGIN) != text.count(END) or text.count(BEGIN) > 1:
        raise ValueError('Ambiguous existing hotkey block')
    if BEGIN in text:
        start, end = text.index(BEGIN), text.index(END) + len(END)
        text = text[:start] + text[end:].lstrip('\r\n')
    if install:
        text = text.rstrip('\r\n') + '\n\n' + BLOCK
    return text.encode('utf-8')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'remove'])
    parser.add_argument('--serial', required=True)
    args = parser.parse_args()
    if any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._' for c in args.serial):
        parser.error('Invalid serial')
    adb = ['adb', '-s', args.serial]
    def shell(command):
        return subprocess.check_output(adb + ['shell', command], text=True).replace('\r', '').strip()
    def call(*parts):
        subprocess.run(adb + list(parts), check=True)
    if args.action == 'install':
        if shell('test -x /storage/apps/current/shared/c1max-hotkey && echo READY') != 'READY':
            raise SystemExit('Deploy the runtime with c1max-hotkey first')
    root = pathlib.Path(__file__).resolve().parents[1]
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
    backup = root / '.runtime' / 'backups' / args.serial / ('hotkey-' + stamp)
    backup.mkdir(parents=True, mode=0o700)
    call('pull', '/etc/init.rc', str(backup / 'init.rc.before'))
    before = (backup / 'init.rc.before').read_bytes()
    after = patch(before, args.action == 'install')
    staged = backup / 'init.rc.after'
    staged.write_bytes(after)
    expected = hashlib.sha256(before).hexdigest()
    installed = hashlib.sha256(after).hexdigest()
    remote_backup = '/storage/apps/backups/hotkey-' + stamp
    call('shell', 'mkdir -p ' + shlex.quote(remote_backup) + '; chmod 700 /storage/apps/backups ' + shlex.quote(remote_backup))
    call('push', str(staged), remote_backup + '/init.rc.after')
    command = f'''set -e
[ "$(sha256sum /etc/init.rc | cut -d ' ' -f 1)" = {expected} ]
cp -p /etc/init.rc {shlex.quote(remote_backup + '/init.rc.before')}
mount -o remount,rw /
trap 'mount -o remount,ro /' EXIT
cp -p /etc/init.rc /etc/init.rc.c1apps.tmp
cat {shlex.quote(remote_backup + '/init.rc.after')} > /etc/init.rc.c1apps.tmp
[ "$(sha256sum /etc/init.rc.c1apps.tmp | cut -d ' ' -f 1)" = {installed} ]
mv /etc/init.rc.c1apps.tmp /etc/init.rc
sync
mount -o remount,ro /
trap - EXIT
echo HOTKEY_CONFIGURED'''
    if shell(command).splitlines()[-1:] != ['HOTKEY_CONFIGURED']:
        raise SystemExit('Configuration did not verify; inspect backup at ' + remote_backup)
    if shell('sha256sum /etc/init.rc').split()[0] != installed:
        raise SystemExit('Installed init.rc checksum mismatch')
    if args.action == 'install':
        call('shell', 'mkdir -p /storage/apps/data/launcher; rm -f /storage/apps/data/launcher/hotkey.disabled; nohup setsid /storage/apps/current/shared/c1max-hotkey </dev/null >>/storage/apps/data/launcher/hotkey.log 2>&1 & sleep 1')
    else:
        # Existing init may know this service after reboot; stop it before the manual instance.
        call('shell', 'touch /storage/apps/data/launcher/hotkey.disabled; setprop ctl.stop c1apps-hotkey')
        print('Hotkey disabled now; boot hook removed. Any manual monitor exits on reboot.')
    print('Backup:', remote_backup + '/init.rc.before')
    print('Host backup:', backup)
    print('Boot hook configured; running systemservice reads the new definition on next boot.')

if __name__ == '__main__':
    main()
