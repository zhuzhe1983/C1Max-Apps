"""将网页安装写入原生应用商店的 generation，保留其回退与菜单约定。"""
import io
import json
import pathlib
import secrets
import shlex
import subprocess
import sys
import tarfile

ROOT=pathlib.Path(__file__).resolve().parents[1]

def console(port,command):
    result=subprocess.run([sys.executable,str(ROOT/'scripts/console.py'),'--url',f'http://127.0.0.1:{port}','--timeout','60',command],capture_output=True,text=True,timeout=70)
    if result.returncode:raise RuntimeError('客体安装失败，请查看串口日志')
    return result.stdout

def install(simulator,port,ident):
    from scripts.fetch_apps import fetch
    audit=fetch(ident)
    simulator.command('RUN launcher')
    output=console(port,"printf '\\nC1MAX_STORE_STATE\\n'; if [ -f /storage/apps/data/appstore/current/state.json ]; then cat /storage/apps/data/appstore/current/state.json; else echo null; fi; printf '\\nC1MAX_STORE_END\\n'")
    current=json.loads(output.split('\nC1MAX_STORE_STATE\n')[-1].split('\nC1MAX_STORE_END\n')[0])
    catalog=json.loads((ROOT/'catalog.json').read_text())
    if current is None:
        installed={a['id']:a.get('installed',False) for a in simulator.apps}
        current={'schema':1,'apps':{a['id']:{'installed':installed.get(a['id'],False),'visible':True,'version':a['version'],'revision':a['revision'],'path':'','title':a['title']} for a in catalog['apps']}}
    if current.get('schema')!=1:raise ValueError('原生商店状态不兼容')
    app=next(a for a in catalog['apps'] if a['id']==ident)
    current['apps'][ident]={'installed':True,'visible':True,'version':app['version'],'revision':app['revision'],'path':'/storage/apps/current/'+ident,'title':app['title']}
    menu=[];entries=[];links=['#!/bin/sh','set -e','for source in /storage/apps/current/*; do [ ! -d "$source" ] || ln -s "$source" "$GEN/${source##*/}"; done']
    for row in sorted(catalog['apps'],key=lambda a:a['id']!='appstore'):
        state=current['apps'].get(row['id'])
        if not state or not state['installed']:continue
        entries.append({k:state[k] for k in ('version','revision')}|{'id':row['id']})
        path=state.get('path','')
        if path:
            if not path.startswith('/storage/apps/') or '\n' in path:raise ValueError('商店应用路径无效')
            links.append('rm -f "$GEN/'+row['id']+'"; ln -s '+shlex.quote(path)+' "$GEN/'+row['id']+'"')
        if state.get('visible',True) or row['id']=='appstore':
            program='c1max-nes-browser' if row['id']=='nes' else 'c1max-'+row['id']
            menu.append(row['title']+'|/storage/apps/data/appstore/current/'+row['id']+'/'+program+'|'+('--roms' if row['id']=='nes' else '')+'|||'+('updates' if row['id']=='appstore' else row['id']))
    meta={'state.json':json.dumps(current,ensure_ascii=False).encode(),'catalog.json':json.dumps({'schema':1,'platform':'c1max-mipsel-linux','apps':entries}).encode(),'launcher-menu.txt':('\n'.join(menu)+'\n').encode(),'links.sh':('\n'.join(links)+'\n').encode()}
    target=ROOT/'build/repository'/(ident+'-runtime.tar.gz')
    with tarfile.open(target,'w:gz') as archive:
        for name,data in meta.items():
            entry=tarfile.TarInfo(name);entry.size=len(data);entry.mode=0o600;archive.addfile(entry,io.BytesIO(data))
    suffix=secrets.token_hex(8)
    home='/storage/apps/data/appstore'
    gen=home+'/generations/host-'+suffix
    command=f'(set -e; GEN={gen}; export GEN; mkdir -p /storage/apps/.incoming-{ident} "$GEN"; /usr/bin/wget -q -O /tmp/{ident}.tar.gz http://10.0.2.2:{port}/repo/{ident}.tar.gz; tar -xzf /tmp/{ident}.tar.gz -C /storage/apps/.incoming-{ident}; cp -a /storage/apps/.incoming-{ident}/{ident} /storage/apps/current/; mkdir -p /storage/apps/data/{ident}; /usr/bin/wget -q -O /tmp/store-meta.tar.gz http://10.0.2.2:{port}/repo/{ident}-runtime.tar.gz; tar -xzf /tmp/store-meta.tar.gz -C "$GEN"; sh "$GEN/links.sh"; rm "$GEN/links.sh"; if [ -e {home}/current ]; then old=$(realpath {home}/current); rm -f {home}/previous.new; ln -s "$old" {home}/previous.new; mv -Tf {home}/previous.new {home}/previous; fi; sync; rm -f {home}/current.new; ln -s "$GEN" {home}/current.new; mv -Tf {home}/current.new {home}/current; rm -rf /storage/apps/.incoming-{ident}; rm /tmp/{ident}.tar.gz /tmp/store-meta.tar.gz; sync)'
    console(port,command)
    simulator.command('INVENTORY')
    return audit
