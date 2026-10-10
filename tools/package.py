#!/usr/bin/env python3
"""Produce a device payload; data/secrets/ROMs are never bundled."""
import argparse, hashlib, json, pathlib, runpy, shutil, subprocess
from PIL import Image, ImageOps
root=pathlib.Path(__file__).resolve().parents[1]
ids=['launcher','piano','nes','streamplayer','calendar','calculator','settings','terminal','gomoku','pcsx4all','processing','dosbox','airtune','crosspoint','camera','mail','bilibili','hidpilot','moonpilot','tox','appstore']
parser=argparse.ArgumentParser()
parser.add_argument('--local',action='store_true',help='Apply ignored config/package.local.py to the device payload')
parser.add_argument('--source-list',type=pathlib.Path,help='NUL-separated git ls-files output from the host for container builds')
args=parser.parse_args()
# Ignore private build inputs when computing the public revisions.
# Include new non-ignored sources as well as existing tracked files.
source_list=args.source_list.read_bytes() if args.source_list else subprocess.check_output(
    ['git','-c','safe.directory='+str(root),'ls-files','--cached','--others','--exclude-standard','-z'],cwd=root
)
public_files={pathlib.Path(p.decode()) for p in source_list.split(b'\0') if p}
tool_payload=root/'linux-tools/.build/linux-tools'
tool_verification=root/'linux-tools/.build/verification.json'
if not tool_verification.is_file():raise SystemExit('Run apps/linux-tools/build.sh before packaging')
for name,entry in json.loads(tool_verification.read_text())['binaries'].items():
    binary=tool_payload/'bin'/name
    if not binary.is_file() or hashlib.sha256(binary.read_bytes()).hexdigest()!=entry['sha256']:
        raise SystemExit('Unverified Linux tool: '+name)
# A CR in a shell rc/script breaks prompts and commands on the device (core.autocrlf checkouts).
for name in ['terminal/assets/shellrc','terminal/assets/inputrc','launcher/run.sh','launcher/apps.txt','launcher/desktop-service.sh']:
    if b'\r' in (root/name).read_bytes():raise SystemExit(name+' has CRLF line endings; re-checkout with LF (see .gitattributes)')
versions={
    'launcher':'0.3.1', 'piano':'0.3.0', 'nes':'0.2.2',
    'streamplayer':'0.3.1', 'calendar':'0.3.1', 'calculator':'0.2.0',
    'settings':'0.2.1', 'terminal':'0.1.0', 'gomoku':'0.1.0',
    'pcsx4all':'0.1.1', 'processing':'0.1.0', 'dosbox':'0.1.2',
    'airtune':'0.3.1', 'crosspoint':'0.4.1', 'camera':'0.2.0',
    'mail':'0.2.1', 'bilibili':'0.2.1', 'hidpilot':'0.2.0',
    'moonpilot':'0.1.1', 'tox':'0.3.1', 'appstore':'0.1.0',
}
apps=[]
for name in ids:
    digest=hashlib.sha256()
    for folder in [root/name,root/'shared',root/'tools',root/'linux-tools']:
        for p in sorted(folder.rglob('*')):
            if p.relative_to(root) not in public_files:continue
            if not p.is_file() or any(x in p.parts for x in ['build','.build','.deps','__pycache__']):continue
            if p.suffix in ['.nes','.7z','.o']:continue
            if p.suffix == '.png' and 'assets' not in p.parts:continue
            digest.update(str(p.relative_to(root)).encode()+b'\0'+p.read_bytes())
    if name=='settings':
        for filename in ['terminal/src/voice.cpp','terminal/src/voice.hpp']:
            digest.update(filename.encode()+b'\0'+(root/filename).read_bytes())
    if name=='tox':
        for filename in ['camera/src/frame.hpp','camera/src/album.cpp','camera/src/album.hpp','camera/src/stb_image_write.h','moonpilot/src/process.hpp']:
            digest.update(filename.encode()+b'\0'+(root/filename).read_bytes())
    if name=='moonpilot':
        for filename in ['camera/src/stb_image_write.h','streamplayer/src/y4m.hpp','streamplayer/src/yuv_pipe.c','crosspoint/vendor/tinyxml2/tinyxml2.cpp','crosspoint/vendor/tinyxml2/tinyxml2.h']:
            digest.update(filename.encode()+b'\0'+(root/filename).read_bytes())
    for filename in ['CMakeLists.txt','dependencies.json','archives.json']:
        digest.update((root/filename).read_bytes())
    apps.append({'id':name,'version':versions[name],'revision':digest.hexdigest()})
catalog={'schema':1,'platform':'c1max-mipsel-linux','apps':apps}
if not args.local:(root/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
out=root/'.build/device'
if out.exists():shutil.rmtree(out)  # Generated payload only, never the runtime data tree.
out.mkdir(parents=True)
for name in ids:
    (out/name).mkdir()
    shutil.copy2(root/'.build/mips'/('c1max-'+name),out/name)
    (out/name/'manifest.json').write_text(json.dumps(next(a for a in apps if a['id']==name),indent=2)+'\n')
for name in ['run.sh','apps.txt','desktop-service.sh']:
    shutil.copy2(root/'launcher'/name,out/'launcher'/name)
    if name.endswith('.sh'):(out/'launcher'/name).chmod(0o755)
shutil.copytree(root/'launcher/licenses',out/'launcher/licenses')
shutil.copy2(root/'.build/mips/c1max-streamplayer',out/'nes/c1max-nes-browser')
shutil.copy2(root/'nes/README.md',out/'nes')
shutil.copytree(root/'nes/licenses',out/'nes/licenses')
shutil.copy2(root/'.build/mips/c1max-yuv-pipe.so',out/'streamplayer')
(out/'streamplayer/licenses').mkdir()
shutil.copy2(root/'streamplayer/vendor/ffmpeg42/COPYING.LGPLv2.1',out/'streamplayer/licenses/FFmpeg-LGPL-2.1.txt')
(out/'launcher/icons').mkdir()
for icon in sorted((root/'launcher/assets/icons').glob('*.png')):
    if icon.relative_to(root) not in public_files:continue
    with Image.open(icon) as source:
        fitted=ImageOps.contain(source.convert('RGBA'),(96,96),Image.Resampling.LANCZOS)
        pixels=Image.new('RGBA',(96,96));pixels.paste(fitted,((96-fitted.width)//2,(96-fitted.height)//2))
        (out/'launcher/icons'/(icon.stem+'.bgra')).write_bytes(pixels.tobytes('raw','BGRA'))
shutil.copytree(root/'terminal/assets',out/'terminal/assets',ignore=shutil.ignore_patterns('build'))
ime_build=root/'.build/rime-data/build'
for required in ['luna_pinyin_simp.prism.bin','luna_pinyin.table.bin','luna_pinyin_simp.schema.yaml']:
    if not (ime_build/required).is_file():raise SystemExit('Missing prebuilt IME data: run tools/build_ime_data.py')
shutil.copytree(ime_build,out/'terminal/assets/rime-data/build')
shutil.copytree(root/'terminal/licenses',out/'terminal/licenses')
shutil.copy2(root/'terminal/vendor/libvterm/LICENSE',out/'terminal/licenses/libvterm.txt')
shutil.copy2(root/'terminal/licenses-term-ime.txt',out/'terminal/licenses/term-ime.txt')
shutil.copy2(root/'terminal/licenses-term-ime-dict.txt',out/'terminal/licenses/term-ime-dict.txt')
for source, name in [
    ('LICENSE','librime-BSD.txt'),
    ('deps/yaml-cpp/LICENSE','yaml-cpp-MIT.txt'),
    ('deps/leveldb/LICENSE','leveldb-BSD.txt'),
    ('deps/marisa-trie/COPYING.md','marisa.txt'),
    ('deps/opencc/LICENSE','OpenCC-Apache-2.0.txt'),
    ('deps/opencc/deps/marisa-0.2.6/COPYING.md','OpenCC-marisa.txt'),
]:
    shutil.copy2(root/'.deps/librime'/source,out/'terminal/licenses'/name)

shutil.copy2(root/'.build/mips/c1max-psx-core',out/'pcsx4all')
shutil.copytree(root/'pcsx4all/licenses',out/'pcsx4all/licenses')
shutil.copy2(root/'pcsx4all/README.md',out/'pcsx4all')
shutil.copy2(root/'.build/mips/c1max-dos-core',out/'dosbox')
shutil.copy2(root/'.build/mips/C1LAB.COM',out/'dosbox')
shutil.copytree(root/'dosbox/licenses',out/'dosbox/licenses')
shutil.copy2(root/'dosbox/README.md',out/'dosbox')
shutil.copy2(root/'processing/api.js',out/'processing')
shutil.copytree(root/'processing/examples',out/'processing/examples')
shutil.copytree(root/'processing/licenses',out/'processing/licenses')
shutil.copy2(root/'processing/README.md',out/'processing')
shutil.copy2(root/'crosspoint/README.md',out/'crosspoint')
shutil.copytree(root/'crosspoint/licenses',out/'crosspoint/licenses')
shutil.copy2(root/'.deps/libmobi-0.12/COPYING',out/'crosspoint/LGPL-3.0-libmobi.txt')
shutil.copy2(root/'.deps/xpdf-4.06/COPYING',out/'crosspoint/GPL-2.0-Xpdf.txt')
shutil.copy2(root/'.deps/xpdf-4.06/COPYING3',out/'crosspoint/GPL-3.0-Xpdf.txt')
shutil.copy2(root/'.build/mips/c1max-pdftotext',out/'crosspoint')
shutil.copytree(root/'.deps/xpdf-chinese-simplified',out/'crosspoint/assets/xpdf-chinese-simplified',ignore=shutil.ignore_patterns('.c1-source-sha256'))
shutil.copy2(root/'.deps/mbedtls-2.28.10/LICENSE',out/'mail/LICENSE-MbedTLS.txt')
shutil.copytree(root/'bilibili/licenses',out/'bilibili/licenses')
shutil.copy2(root/'bilibili/README.md',out/'bilibili')
shutil.copy2(root/'.build/mips/c1max-hidpilot-usb',out/'hidpilot')
(out/'hidpilot/c1max-hidpilot-usb').chmod(0o755)
shutil.copytree(root/'hidpilot/licenses',out/'hidpilot/licenses')
shutil.copy2(root/'hidpilot/README.md',out/'hidpilot')
shutil.copytree(root/'moonpilot/licenses',out/'moonpilot/licenses')
shutil.copy2(root/'moonpilot/README.md',out/'moonpilot')
shutil.copytree(root/'tox/licenses',out/'tox/licenses')
shutil.copy2(root/'appstore/README.md',out/'appstore')
(out/'appstore/licenses').mkdir()
shutil.copy2(root/'.deps/mbedtls-2.28.10/LICENSE',out/'appstore/licenses/MbedTLS-Apache-2.0.txt')
for name in ['bootstrap.json','README.md']:
    shutil.copy2(root/'tox'/name,out/'tox'/name)
shutil.copytree(root/'camera/licenses',out/'camera/licenses')
shutil.copytree(root/'camera/assets',out/'camera/assets')
shutil.copytree(tool_payload,out/'linux-tools')
shutil.copy2(tool_verification,out/'linux-tools/share/verification.json')
(out/'shared').mkdir()
shutil.copy2(root/'.build/ca-certificates.crt',out/'shared')
shutil.copy2(root/'.build/mips/c1max-activate',out/'shared')
shutil.copy2(root/'.build/mips/c1max-capture',out/'shared')
shutil.copy2(root/'.build/mips/c1max-volume',out/'shared')
shutil.copy2(root/'.build/mips/c1max-power-guard',out/'shared')
shutil.copy2(root/'.build/mips/c1max-hotkey',out/'shared')
for owner in ['shared','piano','airtune','streamplayer']:
    shutil.copy2(root/'.build/mips/c1max-audio',out/owner)
# Store apps remain usable when Terminal is not installed.
for app in ['crosspoint','airtune','streamplayer','bilibili','calendar','mail','settings','moonpilot','tox']:
    shutil.copytree(out/'terminal/assets/rime-data',out/app/'assets/rime-data')
    shutil.copytree(out/'terminal/licenses',out/app/'licenses/ime')
font=root/'shared/fonts/NotoSansSC-Regular.ttf'
if font.exists():shutil.copy2(font,out/'shared')
shutil.copy2(root/'shared/fonts/OFL.txt',out/'shared/NotoSansSC-OFL.txt')
shutil.copytree(root/'shared/licenses',out/'shared/licenses')
# This opt-in hook may append private manifests/assets/menu entries to the
# in-memory catalog. The resulting catalog is written only inside .build.
hook=root/'config/package.local.py'
if args.local and hook.is_file():
    runpy.run_path(str(hook),init_globals={'root':root,'payload':out,'catalog':catalog})
(out/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
checks=[]
for p in sorted(out.rglob('*')):
    if p.is_file():checks.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(out)))
(out/'SHA256SUMS').write_text('\n'.join(checks)+'\n')
print('Packaged',len(checks),'files;',sum(p.stat().st_size for p in out.rglob('*') if p.is_file()),'bytes')
