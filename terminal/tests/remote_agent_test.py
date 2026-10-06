#!/usr/bin/env python3
"""Exercise the shipped SSH/agent scripts with fake tools; never connect."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RemoteAgentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.bin = self.root / 'terminal/assets/bin'
        self.bin.mkdir(parents=True)
        for name in ('ssh', 'agent'):
            shutil.copy2(ROOT / 'terminal/assets/bin' / name, self.bin / name)
        tools = self.root / 'linux-tools/bin'
        tools.mkdir(parents=True)
        client = tools / 'dbclient'
        client.write_text(f'#!{sys.executable}\nimport json, os, sys\n'
                          'open(os.environ["ARGV_LOG"], "w").write(json.dumps(sys.argv[1:]))\n')
        keytool = tools / 'dropbearkey'
        keytool.write_text('#!/bin/sh\n[ "$1" = -y ] && [ "$2" = -f ] && '
                           '[ "$(cat "$3")" = DROPBEAR ]\n')
        for tool in (client, keytool):
            tool.chmod(0o755)
        self.home = self.root / 'home'
        (self.home / '.ssh').mkdir(parents=True)
        self.log = self.root / 'argv.json'
        self.data = self.root / 'data'
        self.env = dict(os.environ, HOME=str(self.home), C1_APPS_DATA=str(self.data),
                        ARGV_LOG=str(self.log))

    def run_tool(self, name, *args, ok=True):
        result = subprocess.run([str(self.bin / name), *args], env=self.env,
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode == 0, ok, result.stderr)
        return result

    def argv(self):
        return json.loads(self.log.read_text())

    def test_key_format_and_explicit_identity(self):
        # An OpenSSH filename alone is not proof of Dropbear compatibility.
        (self.home / '.ssh/id_ed25519').write_text('OPENSSH')
        self.run_tool('ssh', 'user@host')
        self.assertEqual(self.argv(), ['user@host'])
        key = self.home / '.ssh/id_ecdsa'
        key.write_text('DROPBEAR')
        self.run_tool('ssh', '-t', 'user@host')
        self.assertEqual(self.argv(), ['-i', str(key), '-t', 'user@host'])
        for args in (('-i', '/explicit', 'user@host'), ('-i/explicit', 'user@host')):
            self.run_tool('ssh', *args)
            self.assertEqual(self.argv(), list(args))

    def test_saved_target_is_private_and_used(self):
        self.run_tool('agent', 'configure', 'dev@host', 'project-1')
        config = self.data / 'terminal/agent.conf'
        self.assertEqual(config.stat().st_mode & 0o777, 0o600)
        self.assertEqual(config.parent.stat().st_mode & 0o777, 0o700)
        self.assertEqual(list(config.parent.iterdir()), [config])
        self.run_tool('agent')
        args = self.argv()
        self.assertEqual(args[:2], ['-t', 'dev@host'])
        self.assertIn('env TERM=xterm-256color', args[2])
        self.assertIn('--session project-1', args[2])

    def test_reject_option_and_shell_injection(self):
        for target in ('-oProxyCommand=bad', 'host', '@host', 'user@',
                       'a@b@c', "user@host';touch BAD", 'x'*321+'@host'):
            self.run_tool('agent', target, ok=False)
        for session in ('--help', 'name;exit', "name'", 'x'*129):
            self.run_tool('agent', 'user@host', session, ok=False)
        self.assertFalse(self.log.exists())
        self.run_tool('agent', 'configure', 'user@host')
        config = self.data / 'terminal/agent.conf'
        config.write_text('target=-oProxyCommand=bad\nsession=ok\n')
        self.run_tool('agent', ok=False)
        self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
