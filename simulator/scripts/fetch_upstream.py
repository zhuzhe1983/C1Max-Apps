#!/usr/bin/env python3
"""获取固定提交的公开基础运行时源码；不修改已有源码工作区。"""
import hashlib
import json
import pathlib
import subprocess

ROOT=pathlib.Path(__file__).resolve().parents[1]

def main():
    spec=json.loads((ROOT/'upstream.json').read_text())
    directory=ROOT/'.cache/upstream/C1Max-Apps'
    directory.parent.mkdir(parents=True,exist_ok=True)
    if not directory.exists():
        subprocess.run(['git','init',str(directory)],check=True)
        subprocess.run(['git','-C',str(directory),'remote','add','origin',spec['repository']],check=True)
        subprocess.run(['git','-C',str(directory),'fetch','--depth','1','origin',spec['revision']],check=True)
        subprocess.run(['git','-C',str(directory),'checkout','--detach',spec['revision']],check=True)
    head=subprocess.check_output(['git','-C',str(directory),'rev-parse','HEAD'],text=True).strip()
    dirty=subprocess.check_output(['git','-C',str(directory),'status','--porcelain','--untracked-files=no'],text=True)
    if head!=spec['revision'] or dirty:raise SystemExit('基础源码提交不匹配或有本地修改；请保留修改并另设干净缓存。')
    header=directory.parent/'stb_truetype_htcw.h'
    expected=spec['lvgl']['header_sha256']
    if not header.exists() or hashlib.sha256(header.read_bytes()).hexdigest()!=expected:
        temporary=header.with_suffix('.part')
        url='https://raw.githubusercontent.com/lvgl/lvgl/'+spec['lvgl']['commit']+'/src/libs/tiny_ttf/stb_truetype_htcw.h'
        subprocess.run(['curl','-fL','--retry','2','--connect-timeout','20','--max-time','120','-o',str(temporary),url],check=True)
        if hashlib.sha256(temporary.read_bytes()).hexdigest()!=expected:raise SystemExit('stb 字体依赖校验失败')
        temporary.replace(header)
    print('基础运行时源码已校验：'+head,flush=True)

if __name__=='__main__':main()
