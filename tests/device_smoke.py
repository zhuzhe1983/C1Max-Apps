#!/usr/bin/env python3
"""Host-side C1Max GUI smoke tests. No ADB runs on import, prepare or self-test.

The operator launches a generated wrapper in the foreground first. Tests refuse
ordinary app data, another GUI, or a launcher not waiting for this child.
"""
import argparse
import base64
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import time
import uuid

LETTERS = "qwertyuiopasdfghjklzxcvbnm"
CODES = list(range(16, 26)) + list(range(30, 39)) + list(range(44, 51))
KEYS = dict(zip(LETTERS, CODES))
SHIFT = dict(zip("1234567890~@#$%&*().-/?:;,", LETTERS))
EXTRA = dict(zip("=+_|\\\"'<>![]{}`^", "qwertyuiopasdfgh"))
POWER, ENTER, BACKSPACE, SYMBOL = 116, 28, 111, 410
APPS = ("calendar", "terminal", "streamplayer", "calculator", "piano", "nes")


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def quote(value):
    return shlex.quote(str(value))


def key_device(code):
    return 1 if code in (14, ENTER, BACKSPACE, POWER, SYMBOL) else 0


def text_keys(text, terminal=False):
    """Return (event-device, chord) actions for a fresh app's lowercase map."""
    result = []
    for char in text:
        if char in KEYS:
            result.append((0, [KEYS[char]]))
        elif char == ' ':
            result.append((0, [57]))
        elif char == '\n':
            result.append((1, [ENTER]))
        elif char in SHIFT:
            result.append((0, [42, KEYS[SHIFT[char]]]))
        elif terminal and char in EXTRA:
            result.extend([(1, [SYMBOL])] * 3)
            result.append((0, [KEYS[EXTRA[char]]]))
        else:
            raise ValueError(f'Unsupported text {char!r}; use lowercase; extra punctuation needs terminal mode')
    return result


def native_touch(x, y):
    if not (0 <= x < 800 and 0 <= y < 340):
        raise ValueError('Logical touch must be within 800x340')
    return y, 799 - x


def unwrap(output, marker, returncode):
    output = output.replace(b'\r\n', b'\n')
    match = re.search(rb'\n' + marker.encode() + rb':([0-9]+)\n?$', output)
    require(match is not None, 'Missing ADB completion marker; do not trust legacy adbd exit status')
    require(returncode == 0 and int(match.group(1)) == 0,
            'Remote command failed: ' + output.decode(errors='replace')[-1200:])
    return output[:match.start()]


def proc_identity(raw):
    # comm can contain spaces or closing parentheses. starttime is field 22.
    tail = raw.rsplit(') ', 1)[1].split()
    return raw.split(' ', 1)[0], tail[19], tail[1]


def proc_children(records, parent):
    # NUL-separated records also handle a process comm containing a newline.
    identities = [proc_identity(raw.decode()) for raw in records.split(b'\0') if raw]
    return sorted((item for item in identities if item[2] == str(parent)), key=lambda item: int(item[0]))


def parse_calendar(raw):
    lines = raw.decode().splitlines()
    require(lines and lines[0] == 'C1MAX_CALENDAR_V1', 'Invalid calendar store header')
    events = []
    for line in lines[1:]:
        fields = line.split('\t')
        require(len(fields) == 10 and fields[0] == 'E', 'Unexpected test calendar record')
        events.append(dict(zip(('kind', 'id', 'start', 'end', 'all_day', 'start_time',
                               'end_time', 'title', 'location', 'note'), fields)))
    return events


class Device:
    def __init__(self, args):
        self.args = args
        self.path = Path(args.session).resolve()
        self.session = json.loads(self.path.read_text())
        self.remote = self.session['remote']
        require(re.fullmatch(r'/storage/apps/\.smoke-[0-9a-f]{12}', self.remote), 'Invalid isolated test directory')
        require(self.session['root'] == '/storage/apps/current', 'Tests require the formal app release')
        self.data = self.remote + '/data'
        self.pid = str(args.pid)
        require(self.pid.isdigit() and int(self.pid) > 1, 'Pass the foreground test app PID')
        self.identity = None
        self.app = args.app
        self.artifacts = self.path.parent / 'artifacts'
        self.artifacts.mkdir(exist_ok=True)
        self.guard()

    def shell(self, body, timeout=90):
        marker = 'C1SMOKE_' + uuid.uuid4().hex
        script = '(\nset -e\n' + body + '\n)\nrc=$?\nprintf "\\n' + marker + ':%s\\n" "$rc"\nexit "$rc"'
        result = subprocess.run([self.args.adb, '-s', self.args.serial, 'shell', script],
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=timeout)
        return unwrap(result.stdout, marker, result.returncode)

    def guard(self):
        pid = self.pid
        raw = self.shell(f'cat /proc/{pid}/stat').decode()
        identity = proc_identity(raw)
        if self.identity:
            require(identity[:2] == self.identity[:2], 'Foreground PID changed/reused; stopping input')
        self.identity = identity
        expected = f'C1_APPS_DATA={self.data}'
        env = self.shell(f"tr '\\000' '\\n' < /proc/{pid}/environ | sed -n '/^C1_APPS_DATA=/p'").decode().strip()
        require(env == expected, 'Refusing input: application does not use the isolated C1_APPS_DATA')
        self.shell(f'test "$(readlink /proc/{pid}/exe)" = "$(readlink -f /storage/apps/current/{self.app}/c1max-{self.app})"')
        # A launcher waiting for this app is safe; a separately running launcher
        # would receive the same injected events, even if temporarily SIGSTOPped.
        self.shell(f'test "$(pidof c1max-{self.app})" = {quote(pid)}\n'
                   '! pidof mp_s300 >/dev/null 2>&1\n'
                   'for p in $(pidof c1max-launcher 2>/dev/null || true); do '
                   f'test "$p" = {quote(identity[2])}; done\n' + '\n'.join(
                       f'! pidof c1max-{name} >/dev/null 2>&1' for name in APPS if name != self.app))

    def live_prefix(self):
        # Repeat identity and data checks inside each mutating shell batch.
        return (f's=$(cat /proc/{self.pid}/stat); s=${{s##*) }}; set -- $s; shift 19; '
                f'test "$1" = {quote(self.identity[1])}\n'
                f'test "$(tr \'\\000\' \'\\n\' < /proc/{self.pid}/environ | sed -n \'/^C1_APPS_DATA=/p\')" '
                f'= {quote("C1_APPS_DATA=" + self.data)}\n')

    def actions(self, actions):
        lines, length = [], 0
        for device, codes in actions:
            line = quote(self.args.key_helper) + f' {device} ' + ' '.join(str(c) for c in codes)
            if length + len(line) > 1600:
                self.shell(self.live_prefix() + '\n'.join(lines)); lines, length = [], 0
            lines.append(line); length += len(line) + 1
        if lines:
            self.shell(self.live_prefix() + '\n'.join(lines))

    def key(self, code, count=1):
        self.actions([(key_device(code), [code])] * count)

    def text(self, value):
        self.actions(text_keys(value, terminal=self.app == 'terminal'))

    def tap(self, x, y):
        ax, ay = native_touch(x, y)
        output = self.shell(self.live_prefix() + f'{quote(self.args.touch_helper)} {ax} {ay} {ax} {ay} 1 100')
        # The old touch helper reports write failures but returns zero.
        require(not output.strip(), 'Touch injector reported an error: ' + output.decode(errors='replace'))
        time.sleep(0.25)

    def read(self, relative):
        require('..' not in relative.split('/') and not relative.startswith('/'), 'Invalid test artifact path')
        encoded = self.shell('base64 ' + quote(self.remote + '/' + relative))
        return base64.b64decode(encoded, validate=False)

    def capture(self, name):
        self.guard()
        path = self.remote + '/' + name + '.raw'
        output = self.shell('/storage/apps/current/shared/c1max-capture ' + quote(path))
        require(re.search(rb'^CAPTURED bytes=1088000 ', output, re.M), 'Capture helper did not confirm a fresh frame')
        local = self.artifacts / (name + '.raw')
        local.unlink(missing_ok=True)
        subprocess.run([self.args.adb, '-s', self.args.serial, 'pull', path, str(local)], check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
        require(local.stat().st_size == 1088000, 'Incomplete capture')
        if shutil.which('ffmpeg'):
            subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pixel_format',
                            'bgra', '-video_size', '340x800', '-i', str(local), '-frames:v', '1',
                            '-vf', 'transpose=1', str(local.with_suffix('.png'))], check=True)

    def same_process(self, pid, stamp):
        raw = self.shell(f'if [ -r /proc/{pid}/stat ]; then cat /proc/{pid}/stat; fi').decode()
        return bool(raw) and proc_identity(raw)[:2] == (pid, stamp)

    def wait_gone(self, pid, stamp):
        until = time.monotonic() + 8
        while time.monotonic() < until:
            if not self.same_process(pid, stamp):
                return
            time.sleep(0.25)
        raise RuntimeError('Process did not exit; left intact for inspection')

    def exit_app(self):
        self.key(POWER)
        self.wait_gone(*self.identity[:2])

    def event(self, title):
        events = parse_calendar(self.read('data/calendar/calendar.db'))
        require(len(events) == 1, 'Expected exactly one isolated calendar event')
        event = events[0]
        require(event['title'] == title and event['start'] == '2031-04-09' and event['end'] == '2031-04-09',
                'Calendar title/date did not match physical input: ' + repr(event))
        return event

    def save_state(self):
        self.path.write_text(json.dumps(self.session, indent=2) + '\n')

    def calendar_create(self):
        self.shell('test ! -e ' + quote(self.data + '/calendar/calendar.db'))
        title = 'smoke' + self.session['id'][:6]
        self.text('n'); time.sleep(0.3)
        self.text(title)
        self.key(ENTER); self.key(BACKSPACE, 10); self.text('2031-04-09')
        self.key(ENTER, 2); self.key(BACKSPACE, 10); self.text('2031-04-09')
        self.tap(620, 26)
        original = self.event(title)
        self.text('e'); time.sleep(0.25)
        self.text('x'); self.key(BACKSPACE); self.text('edit')
        self.tap(620, 26)
        edited = self.event(title + 'edit')
        require(original['id'] == edited['id'], 'Edit replaced the event identity')
        self.capture('calendar-created-edited')
        self.session['calendar'] = {'identity': self.identity[:2], 'id': edited['id'], 'title': edited['title']}
        self.exit_app()
        self.event(edited['title'])
        self.save_state()
        print('PASS create, date input, Backspace, edit, disk save and POWER exit. Relaunch calendar wrapper, then calendar-reload-delete.')

    def calendar_reload_delete(self):
        prior = self.session.get('calendar')
        require(prior is not None and list(self.identity[:2]) != prior['identity'], 'A fresh calendar process is required')
        require(self.event(prior['title'])['id'] == prior['id'], 'Saved event changed before relaunch')
        self.text('l'); time.sleep(0.25); self.key(ENTER); time.sleep(0.25)
        self.text('e'); time.sleep(0.25); self.text('reload'); self.tap(620, 26)
        require(self.event(prior['title'] + 'reload')['id'] == prior['id'], 'UI did not reload the saved event')
        self.capture('calendar-reloaded')
        self.text('d'); time.sleep(0.25); self.tap(554, 241)
        require(parse_calendar(self.read('data/calendar/calendar.db')) == [], 'Delete did not persist')
        self.capture('calendar-deleted'); self.exit_app()
        self.session['calendar']['passed'] = True; self.save_state()
        print('PASS new-process reload, UI edit of persisted ID, confirmed deletion and POWER exit.')

    def terminal_test(self):
        self.shell('test ! -e ' + quote(self.data + '/terminal/shellproof') + '\n'
                   'test ! -e ' + quote(self.data + '/terminal/homeproof'))
        # This kernel omits /proc/PID/task/PID/children. Every normal process
        # still has stat; tolerate processes disappearing during enumeration.
        records = self.shell(self.live_prefix() +
                             'for f in /proc/[0-9]*/stat; do\n'
                             '  if s=$(cat "$f" 2>/dev/null); then printf \'%s\\000\' "$s"; fi\n'
                             'done')
        children = proc_children(records, self.pid)
        require(len(children) == 1, 'Expected one terminal shell child by PPID')
        shell_identity = proc_identity(self.shell(f'cat /proc/{children[0][0]}/stat').decode())
        require(shell_identity == children[0], 'Shell child changed/reused during enumeration')
        self.guard()
        self.text("printf '%s\\n' 'c1_smoke=ok' | tee shellproof\n")
        time.sleep(0.5)
        require(self.read('data/terminal/shellproof') == b'c1_smoke=ok\n', 'Physical shell command/output mismatch')
        self.text('pwd > homeproof\n'); time.sleep(0.3)
        require(self.read('data/terminal/homeproof').decode().strip() == self.data + '/terminal', 'Shell escaped test HOME/cwd')
        self.capture('terminal-output')
        self.text('exit 0\n'); self.wait_gone(*shell_identity[:2]); self.guard()
        self.capture('terminal-shell-exited'); self.exit_app()
        self.session['terminal_passed'] = True; self.save_state()
        print('PASS physical text/punctuation, shell output, isolated HOME, shell exit and POWER exit. Inspect screenshots for rendered output/status.')


def prepare(directory):
    directory = Path(directory).resolve(); directory.mkdir(parents=True, exist_ok=False)
    token = uuid.uuid4().hex[:12]
    remote = '/storage/apps/.smoke-' + token
    plan = {'id': token, 'remote': remote, 'root': '/storage/apps/current'}
    (directory / 'session.json').write_text(json.dumps(plan, indent=2) + '\n')
    for app in ('calendar', 'terminal'):
        script = ('#!/bin/sh\nset -eu\numask 077\n'
                  f'export C1_APPS_ROOT=/storage/apps/current\nexport C1_APPS_DATA={quote(remote + "/data")}\n'
                  'mkdir -p "$C1_APPS_DATA"\n'
                  f'printf "%s\\n" "$$" > {quote(remote + "/" + app + ".pid")}\n'
                  f'exec "$C1_APPS_ROOT/{app}/c1max-{app}"\n')
        path = directory / (app + '.sh'); path.write_text(script); path.chmod(0o755)
    print(json.dumps(plan, indent=2))
    print('Host files only. Deploy wrappers/helpers manually, launch one wrapper safely, then pass its PID.')


def self_test():
    assert native_touch(0, 0) == (0, 799)
    assert native_touch(799, 339) == (339, 0)
    assert native_touch(620, 26) == (26, 179)
    assert text_keys('1-/') == [(0, [42, 16]), (0, [42, 45]), (0, [42, 46])]
    assert text_keys('|', True) == [(1, [410])] * 3 + [(0, [19])]
    assert text_keys('=', True)[-1] == (0, [16])
    text_keys("printf '%s\\n' 'c1_smoke=ok' | tee shellproof\n", True)
    assert unwrap(b'payload\r\nmarker:0\r\n', 'marker', 0) == b'payload'
    for bad in (b'payload\n', b'payload\nmarker:2\n', b'marker:0\nstale'):
        try: unwrap(bad, 'marker', 0)
        except RuntimeError: pass
        else: raise AssertionError('Accepted missing/failed/stale marker')
    assert parse_calendar(b'C1MAX_CALENDAR_V1\n') == []
    # PID reuse checks do not mistake spaces/parentheses in comm for fields.
    stat = '42 (name with ) space) ' + ' '.join(['S', '7'] + ['0'] * 17 + ['98765', '0'])
    assert proc_identity(stat) == ('42', '98765', '7')
    child = '55 (shell\nwith ) name) ' + ' '.join(['S', '42'] + ['0'] * 17 + ['88888', '0'])
    records = (stat + '\0' + child + '\0').encode()
    assert proc_children(records, '42') == [('55', '88888', '42')]
    assert proc_children(records, '9') == []
    assert len(SHIFT) == 26 and len(EXTRA) == 16
    print('PASS host-only rotation, physical key/punctuation plans, markers, calendar format. No ADB invoked.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    sub.add_parser('self-test')
    prep = sub.add_parser('prepare'); prep.add_argument('directory')
    for name, app in [('calendar-create', 'calendar'), ('calendar-reload-delete', 'calendar'), ('terminal', 'terminal')]:
        item = sub.add_parser(name); item.set_defaults(app=app)
        item.add_argument('--session', required=True); item.add_argument('--pid', required=True, type=int)
        item.add_argument('--serial', default=os.environ.get('ANDROID_SERIAL'))
        item.add_argument('--adb', default='adb')
        item.add_argument('--key-helper', default='/tmp/c1max-key-test')
        item.add_argument('--touch-helper', default='/tmp/c1max-touch')
    args = parser.parse_args()
    if args.command == 'self-test': self_test(); return
    if args.command == 'prepare': prepare(args.directory); return
    require(args.serial, 'Pass --serial or set ANDROID_SERIAL; implicit device selection is disabled')
    device = Device(args)
    {'calendar-create': device.calendar_create, 'calendar-reload-delete': device.calendar_reload_delete,
     'terminal': device.terminal_test}[args.command]()


if __name__ == '__main__':
    main()
