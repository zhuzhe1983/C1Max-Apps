#!/usr/bin/env python3
"""下载固定目录中的原始 C1 Max 包，逐文件校验并保留设备 ELF。"""
import argparse
import concurrent.futures
import hashlib
import io
import json
import pathlib
import re
import struct
import subprocess
import tarfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEFAULT = ('calculator', 'calendar', 'piano', 'terminal', 'gomoku', 'crosspoint', 'appstore')


def elf(data):
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 8:
        raise ValueError('要求 ELF32 小端 MIPS')
    offset = struct.unpack_from('<I', data, 28)[0]
    size, count = struct.unpack_from('<HH', data, 42)
    if size < 32 or offset + size * count > len(data): raise ValueError('ELF 程序头不完整')
    if any(struct.unpack_from('<I', data, offset + size * i)[0] == 3 for i in range(count)):
        raise ValueError('要求静态设备程序')
    return {'sha256': hashlib.sha256(data).hexdigest(), 'size': len(data), 'elf_flags': hex(struct.unpack_from('<I', data, 36)[0])}


def fetch(ident):
    catalog = json.loads((ROOT / 'catalog.json').read_text())
    app = next(row for row in catalog['apps'] if row['id'] == ident)
    if not re.fullmatch('[a-z][a-z0-9-]{0,31}', ident): raise ValueError('无效应用 ID')
    package = ROOT / '.cache/packages' / (app['sha256'] + '.c1pkg')
    package.parent.mkdir(parents=True, exist_ok=True)
    valid = package.is_file() and package.stat().st_size == app['size'] and hashlib.sha256(package.read_bytes()).hexdigest() == app['sha256']
    if not valid:
        temporary = package.with_suffix('.part')
        subprocess.run(['curl', '-fL', '--retry', '2', '--connect-timeout', '15', '--max-time', '180',
                        '--silent', '--show-error', '-o', str(temporary), app['url']], check=True)
        data = temporary.read_bytes()
        if len(data) != app['size'] or hashlib.sha256(data).hexdigest() != app['sha256']:
            temporary.unlink(missing_ok=True); raise ValueError('官方目录中的包大小/哈希不一致：' + ident)
        temporary.replace(package)
    data = package.read_bytes()
    if data[:8] != b'C1PKG01\n': raise ValueError('未知 C1 Max 包格式')
    header_size = struct.unpack_from('<I', data, 8)[0]
    if header_size > 1048576 or 12 + header_size > len(data): raise ValueError('包清单长度错误')
    manifest = json.loads(data[12:12 + header_size])
    if any(manifest[key] != app[key] for key in ('id', 'version', 'revision')) or manifest['runtime'] != 1:
        raise ValueError('包清单与固定目录不一致')
    directory = ROOT / 'build/payload'
    directory.mkdir(parents=True, exist_ok=True)
    offset = 12 + header_size
    files = []
    seen = set()
    for row in manifest['files']:
        path = pathlib.PurePosixPath(row['path'])
        if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0] != ident or str(path) in seen:
            raise ValueError('包内路径错误')
        seen.add(str(path))
        size = row['size']; content = data[offset:offset + size]; offset += size
        if size < 0 or len(content) != size or hashlib.sha256(content).hexdigest() != row['sha256'] or row['mode'] not in (0o644, 0o755):
            raise ValueError('包内文件校验失败')
        files.append((str(path), content, row['mode']))
    if offset != len(data): raise ValueError('包含多余数据')
    binary = next(content for name, content, _ in files if name == ident + '/c1max-' + ident)
    audit = {**app, 'package_sha256': app['sha256'], 'source_type': 'official-package', 'entry': ident + '/c1max-' + ident, **elf(binary)}
    # Validate everything before extracting. Never write the runtime data tree.
    for name, content, mode in files:
        target = directory / name; target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content); target.chmod(mode)
    target = directory / ident / 'manifest.json'
    target.write_text(json.dumps({key: app[key] for key in ('id','version','revision')}) + '\n')
    files.append((ident + '/manifest.json', target.read_bytes(), 0o644))
    repository = ROOT / 'build/repository'; repository.mkdir(exist_ok=True)
    with tarfile.open(repository / (ident + '.tar.gz'), 'w:gz') as archive:
        for name, content, mode in files:
            entry = tarfile.TarInfo(name); entry.size = len(content); entry.mode = mode
            archive.addfile(entry, io.BytesIO(content))
    (directory / ident / 'c1sim-audit.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2)+'\n')
    print('已校验官方包：' + ident + ' ' + app['version'], flush=True)
    return audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apps', nargs='*', help='默认预装七个应用；all 下载全部目录应用')
    args = parser.parse_args()
    apps = args.apps or DEFAULT
    if apps == ['all']: apps = [row['id'] for row in json.loads((ROOT/'catalog.json').read_text())['apps']]
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool: list(pool.map(fetch, apps))


if __name__ == '__main__': main()
