#!/usr/bin/env python3
"""Exercise the actual boot preference block with a recording property setter."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'launcher/desktop-service.sh').read_text()
block = source.split('# BEGIN screenoff preference:', 1)[1].split('\n', 1)[1].split('# END screenoff preference', 1)[0]
run_source = (root / 'launcher/run.sh').read_text()
run_block = 'apply_screenoff_preference() {' + run_source.split('apply_screenoff_preference() {', 1)[1].split('\n}\n', 1)[0] + '\n}'
# The preference must be outside the optional ADB block.
assert source.split('# BEGIN screenoff preference:', 1)[0].rstrip().endswith('fi')
block = block.replace('screenoff=/storage/apps/data/settings/screenoff', 'screenoff="$TEST_PREF"')
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory) / 'screenoff'
    def run(text):
        if text is None:
            if path.exists(): path.unlink()
        else: path.write_text(text)
        script = 'set -eu\nsetprop() { printf "%s=%s\\n" "$1" "$2"; }\nlog() { :; }\n' + block
        return subprocess.check_output(['sh', '-c', script], env={**os.environ, 'TEST_PREF': str(path)}, text=True).splitlines()
    assert run(None) == []
    for timer in (30000, 60000, 120000, 300000, 600000, 1200000, 1800000):
        assert run(f'lock=0\ntimer={timer}\n') == [f'sys.backlight.timer={timer}', 'sys.backlight.lock=0', 'sys.backlight.timer.reset=1']
    assert run('lock=1\ntimer=0') == ['sys.backlight.lock=1', 'sys.backlight.timer.reset=1']
    for bad in ('', 'lock=0\n', 'timer=30000\n', 'lock=0\ntimer=0\n', 'lock=2\ntimer=30000\n', 'lock=1\nlock=0\ntimer=30000\n', 'lock=0\ntimer=-1\n', 'lock=0\ntimer=30000;true\n', 'lock=0\ntimer=999999999999999999999999\n'):
        assert run(bad) == [], bad

    run_block = run_block.replace('screenoff=/storage/apps/data/settings/screenoff', 'screenoff="$TEST_PREF"')
    def run_recovery(text):
        if text is None:
            if path.exists(): path.unlink()
        else: path.write_text(text)
        script = 'set -eu\nsetprop() { printf "%s=%s\\n" "$1" "$2"; }\nlog() { :; }\n' + run_block + '\napply_screenoff_preference'
        return subprocess.check_output(['sh', '-c', script], env={**os.environ, 'TEST_PREF': str(path)}, text=True).splitlines()
    assert run_recovery(None) == []
    assert run_recovery('lock=0\ntimer=30000\n') == ['sys.backlight.timer=30000', 'sys.backlight.lock=0', 'sys.backlight.timer.reset=1']
    assert run_recovery('lock=1\ntimer=0\n') == ['sys.backlight.lock=1', 'sys.backlight.timer.reset=1']
    assert run_recovery('lock=0\ntimer=999999999999999999999999\n') == []
print('PASS screen-off replay: all settings, missing/corrupt/duplicate preferences, no forced always-on, timer before unlock')
