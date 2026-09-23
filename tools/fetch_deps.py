#!/usr/bin/env python3
import json, pathlib, subprocess
root = pathlib.Path(__file__).resolve().parents[1]
for name, spec in json.loads((root/'dependencies.json').read_text()).items():
    dest = root/'.deps'/name
    if not dest.exists():
        dest.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(['git','clone','--no-checkout',spec['url'],str(dest)],check=True)
    current = subprocess.check_output(['git','-C',str(dest),'rev-parse','HEAD'],text=True).strip()
    if current != spec['commit']:
        subprocess.run(['git','-C',str(dest),'fetch','origin',spec['commit']],check=True)
        subprocess.run(['git','-C',str(dest),'checkout','--detach',spec['commit']],check=True)
