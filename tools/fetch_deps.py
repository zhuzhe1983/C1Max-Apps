#!/usr/bin/env python3
import hashlib, json, pathlib, subprocess, tarfile, tempfile
root = pathlib.Path(__file__).resolve().parents[1]
for name, spec in json.loads((root/'dependencies.json').read_text()).items():
    dest = root/'.deps'/name
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git','clone','--no-checkout',spec['url'],str(dest)],check=True)
        subprocess.run(['git','-C',str(dest),'checkout','--detach',spec['commit']],check=True)
    current = subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
    if current != spec['commit']:
        subprocess.run(['git','-C',str(dest),'fetch','origin',spec['commit']],check=True)
        subprocess.run(['git','-C',str(dest),'checkout','--detach',spec['commit']],check=True)
for name, spec in json.loads((root/'archives.json').read_text()).items():
    dest=root/'.deps'/name
    marker=dest/'.c1-source-sha256'
    if marker.is_file() and marker.read_text().strip()==spec['sha256']:continue
    cache=root/'.deps/.downloads';cache.mkdir(parents=True,exist_ok=True)
    archive=cache/(spec['url'].rsplit('/',1)[-1])
    if not archive.exists():
        temporary=archive.with_name(archive.name+'.part')
        subprocess.run(['curl','--fail','--location','--retry','2','--max-time','120',spec['url'],'-o',str(temporary)],check=True)
        temporary.rename(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest()!=spec['sha256']:
        raise SystemExit('Source checksum mismatch: '+str(archive))
    if dest.exists():
        # Do not overwrite an unverified/local development source tree.
        raise SystemExit('Existing archive source lacks matching checksum marker: '+str(dest))
    with tempfile.TemporaryDirectory(dir=root/'.deps',prefix='unpack-') as tmp:
        with tarfile.open(archive) as tar:
            tar.extractall(tmp,filter='data')
        source=pathlib.Path(tmp)/name
        if not source.is_dir():raise SystemExit('Unexpected archive root: '+name)
        source.rename(dest)
    marker.write_text(spec['sha256']+'\n')
