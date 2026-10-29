#!/usr/bin/env python3
"""检查运行中的真实 MIPS 客体；建议对独立测试盘执行。"""
import argparse
import json
import pathlib
import subprocess
import time
import urllib.request

ROOT=pathlib.Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--url',default='http://127.0.0.1:8775');args=p.parse_args()
    url=args.url.rstrip('/');dest=ROOT/'build/verification';dest.mkdir(parents=True,exist_ok=True)
    def get(path):
        with urllib.request.urlopen(url+path,timeout=10) as response:return json.load(response)
    initial=get('/api/state');assert initial['ready'],'客体尚未启动'
    def post(path,data):
        request=urllib.request.Request(url+path,json.dumps(data).encode(),{'Content-Type':'application/json','X-C1Sim-Token':initial['token']})
        with urllib.request.urlopen(request,timeout=180) as response:return json.load(response)
    def snapshot(name):
        with urllib.request.urlopen(url+'/api/frame.png') as response:data=response.read()
        assert data.startswith(b'\x89PNG\r\n\x1a\n');(dest/(name+'.png')).write_bytes(data)
    def run(ident):
        post('/api/run',{'id':ident})
        deadline=time.monotonic()+8
        while time.monotonic()<deadline:
            state=get('/api/state')
            if state['app']==ident and state['pid'] and state['exit_code'] is None:break
            time.sleep(.1)
        else:raise AssertionError('应用无法启动：'+ident)
        time.sleep(1);assert get('/api/state')['exit_code'] is None
        snapshot(ident)
    def key(code,source=0):
        for value in (1,0):post('/api/key',{'code':code,'value':value,'source':source});time.sleep(.08)
    for ident in ('calculator','calendar','piano','terminal','gomoku','crosspoint','appstore'):run(ident)
    run('calculator')
    for code in (2,3,30,4,28):key(code)
    time.sleep(.5);snapshot('calculator-result')
    run('terminal')
    for code in (18,46,35,24,57,30,48,46,57,2,3,4,28):key(code)
    time.sleep(.5);snapshot('terminal-input')
    audits=json.loads((ROOT/'build/binaries.json').read_text())
    original=next(a for a in audits if a['id']=='calculator')
    output=subprocess.check_output(['python3',str(ROOT/'scripts/console.py'),'--url',url,'sha256sum /storage/apps/current/calculator/c1max-calculator'],text=True)
    assert original['sha256']+'  /storage/apps/current/calculator/c1max-calculator' in output,'原始 ELF 哈希不符'
    run('launcher')
    (dest/'smoke.json').write_text(json.dumps({'ready':True,'original_elf_verified':True,'apps_started':7,'input_screenshots':['calculator-result.png','terminal-input.png']},indent=2)+'\n')
    print('七个原始 MIPS 应用启动、原始 ELF 哈希和设备输入检查通过；输入结果见真实截图。')

if __name__=='__main__':main()
