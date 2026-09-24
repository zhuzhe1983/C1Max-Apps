#!/usr/bin/env python3
"""Produce a device payload; data/secrets/ROMs are never bundled."""
import hashlib, json, pathlib, shutil
from PIL import Image, ImageOps
root=pathlib.Path(__file__).resolve().parents[1]
ids=['launcher','piano','nes','streamplayer','calendar','calculator','terminal','gomoku','pcsx4all','processing','dosbox','airtune','crosspoint','camera','mail']
tool_payload=root/'linux-tools/.build/linux-tools'
tool_verification=root/'linux-tools/.build/verification.json'
if not tool_verification.is_file():raise SystemExit('Run apps/linux-tools/build.sh before packaging')
for name,entry in json.loads(tool_verification.read_text())['binaries'].items():
    binary=tool_payload/'bin'/name
    if not binary.is_file() or hashlib.sha256(binary.read_bytes()).hexdigest()!=entry['sha256']:
        raise SystemExit('Unverified Linux tool: '+name)
apps=[]
for name in ids:
    digest=hashlib.sha256()
    for folder in [root/name,root/'shared',root/'tools',root/'linux-tools']:
        for p in sorted(folder.rglob('*')):
            if not p.is_file() or any(x in p.parts for x in ['build','.build','.deps','__pycache__']):continue
            if p.suffix in ['.nes','.7z','.o']:continue
            if p.suffix == '.png' and 'assets' not in p.parts:continue
            digest.update(str(p.relative_to(root)).encode()+b'\0'+p.read_bytes())
    for filename in ['CMakeLists.txt','dependencies.json','archives.json']:
        digest.update((root/filename).read_bytes())
    apps.append({'id':name,'version':'0.3.1' if name=='crosspoint' else '0.3.0' if name=='calendar' else '0.1.2' if name=='dosbox' else '0.1.1' if name=='pcsx4all' else '0.1.0' if name in ['terminal','gomoku','processing'] else '0.2.0','revision':digest.hexdigest()})
catalog={'schema':1,'platform':'c1max-mipsel-linux','apps':apps}
(root/'catalog.json').write_text(json.dumps(catalog,indent=2)+'\n')
out=root/'.build/device'
if out.exists():shutil.rmtree(out)  # Generated payload only, never the runtime data tree.
out.mkdir(parents=True)
for name in ids:
    (out/name).mkdir()
    shutil.copy2(root/'.build/mips'/('c1max-'+name),out/name)
    (out/name/'manifest.json').write_text(json.dumps(next(a for a in apps if a['id']==name),indent=2)+'\n')
for name in ['run.sh','apps.txt']:
    shutil.copy2(root/'launcher'/name,out/'launcher'/name)
shutil.copytree(root/'launcher/licenses',out/'launcher/licenses')
shutil.copy2(root/'.build/mips/c1max-yuv-pipe.so',out/'streamplayer')
(out/'streamplayer/licenses').mkdir()
shutil.copy2(root/'streamplayer/vendor/ffmpeg42/COPYING.LGPLv2.1',out/'streamplayer/licenses/FFmpeg-LGPL-2.1.txt')
(out/'launcher/icons').mkdir()
for icon in sorted((root/'launcher/assets/icons').glob('*.png')):
    with Image.open(icon) as source:
        fitted=ImageOps.contain(source.convert('RGBA'),(96,96),Image.Resampling.LANCZOS)
        pixels=Image.new('RGBA',(96,96));pixels.paste(fitted,((96-fitted.width)//2,(96-fitted.height)//2))
        (out/'launcher/icons'/(icon.stem+'.bgra')).write_bytes(pixels.tobytes('raw','BGRA'))
shutil.copytree(root/'terminal/assets',out/'terminal/assets')
(out/'terminal/licenses').mkdir()
shutil.copy2(root/'terminal/vendor/libvterm/LICENSE',out/'terminal/licenses/libvterm.txt')
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
shutil.copy2(root/'.deps/mbedtls-2.28.10/LICENSE',out/'mail/LICENSE-MbedTLS.txt')
shutil.copytree(root/'camera/licenses',out/'camera/licenses')
shutil.copytree(root/'camera/assets',out/'camera/assets')
shutil.copytree(tool_payload,out/'linux-tools')
shutil.copy2(tool_verification,out/'linux-tools/share/verification.json')
(out/'shared').mkdir()
shutil.copy2(root/'.build/ca-certificates.crt',out/'shared')
shutil.copy2(root/'.build/mips/c1max-activate',out/'shared')
shutil.copy2(root/'.build/mips/c1max-capture',out/'shared')
shutil.copy2(root/'.build/mips/c1max-volume',out/'shared')
shutil.copy2(root/'.build/mips/c1max-hotkey',out/'shared')
font=root/'shared/fonts/NotoSansSC-Regular.ttf'
if font.exists():shutil.copy2(font,out/'shared')
shutil.copy2(root/'shared/fonts/OFL.txt',out/'shared/NotoSansSC-OFL.txt')
shutil.copytree(root/'shared/licenses',out/'shared/licenses')
shutil.copy2(root/'catalog.json',out)
checks=[]
for p in sorted(out.rglob('*')):
    if p.is_file():checks.append(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(out)))
(out/'SHA256SUMS').write_text('\n'.join(checks)+'\n')
print('Packaged',len(checks),'files;',sum(p.stat().st_size for p in out.rglob('*') if p.is_file()),'bytes')
