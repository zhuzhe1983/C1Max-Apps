#!/usr/bin/env python3
"""在独立 Docker/Podman 缓存中构建 C1 Max MIPS 系统与基础运行时。"""
import argparse
import json
import pathlib
import shutil
import subprocess
import sys

ROOT=pathlib.Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine',choices=('podman','docker'))
    parser.add_argument('--runtime-only',action='store_true',help='已完整构建后，仅重建设备适配与基础运行时')
    args=parser.parse_args();engine=args.engine;started=False
    if not engine:
        engine=next((x for x in ('podman','docker') if shutil.which(x) and subprocess.run([x,'info'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode==0),None)
        if not engine and sys.platform=='darwin' and shutil.which('podman'):engine='podman'
    if not engine:parser.error('需要 Docker 或 Podman；macOS 可安装 Homebrew podman')
    try:
        if engine=='podman' and sys.platform=='darwin' and subprocess.run([engine,'info'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL).returncode:
            machines=json.loads(subprocess.check_output([engine,'machine','list','--format','json'],text=True))
            if not machines:parser.error('请先执行 podman machine init --cpus 4 --memory 8192')
            subprocess.run([engine,'machine','start'],check=True);started=True
        for name in ('fetch_upstream.py','fetch_sources.py','fetch_apps.py'):
            subprocess.run([sys.executable,str(ROOT/'scripts'/name)],cwd=ROOT,check=True)
        image='localhost/c1max-builder:bookworm'
        subprocess.run([engine,'build','-t',image,'-f','guest/Dockerfile','.'],cwd=ROOT,check=True)
        script='guest/build-runtime.sh' if args.runtime_only else 'guest/build.sh'
        subprocess.run([engine,'run','--rm','--name','c1max-build','-v',str(ROOT)+':/work','-v','c1max-build:/build',image,'sh',script],cwd=ROOT,check=True)
        if args.runtime_only:subprocess.run([sys.executable,str(ROOT/'scripts/pack_guest.py')],check=True)
        print('构建完成。启动：python3 start.py')
    finally:
        if started:subprocess.run([engine,'machine','stop'],check=False)

if __name__=='__main__':main()
