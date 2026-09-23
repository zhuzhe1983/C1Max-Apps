#!/usr/bin/env python3
"""Fetch the fixed official source artifacts; never accept a changed digest."""
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent
DOWNLOADS = ROOT / '.build' / 'downloads'


def fetch(item):
    url = item['url']
    if not url.startswith('https://'):
        raise ValueError('Only HTTPS source URLs are accepted')
    path = DOWNLOADS / url.rsplit('/', 1)[-1]
    if not path.is_file():
        partial = path.with_name(path.name + '.partial')
        subprocess.run(['curl', '--fail', '--location', '--proto', '=https',
                        '--tlsv1.2', '--retry', '2', '--max-time', '240',
                        '--output', str(partial), url], check=True)
        if hashlib.sha256(partial.read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError(f'SHA256 mismatch: {url}')
        partial.replace(path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != item['sha256']:
        raise RuntimeError(f'SHA256 mismatch: {path}; remove it to download again')


if __name__ == '__main__':
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((ROOT / 'sources.json').read_text())
    for source in manifest['sources']:
        fetch(source)
        for patch in source['patches']:
            fetch(patch)
    print('Verified 5 source archives and 20 Bash patches against sources.json')
