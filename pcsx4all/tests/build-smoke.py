#!/usr/bin/env python3
"""Build our own tiny PS-X EXE; no commercial BIOS or game data required."""
import pathlib, struct, subprocess
root=pathlib.Path(__file__).resolve().parents[2]
out=root/'.build/qa/pcsx4all';out.mkdir(parents=True,exist_ok=True)
subprocess.run(['mipsel-linux-gnu-gcc','-nostdlib','-mips1','-mfp32','-msoft-float','-mno-abicalls','-fno-pic','-no-pie','-Wl,-Ttext=0x80010000,-e,_start','-o',str(out/'triangle.elf'),str(root/'pcsx4all/tests/triangle.S')],check=True)
subprocess.run(['mipsel-linux-gnu-objcopy','-O','binary','-j','.text',str(out/'triangle.elf'),str(out/'triangle.bin')],check=True)
code=(out/'triangle.bin').read_bytes();code+=bytes((-len(code))%2048)
header=bytearray(2048);header[:8]=b'PS-X EXE'
struct.pack_into('<4I',header,0x10,0x80010000,0,0x80010000,len(code))
struct.pack_into('<I',header,0x30,0x801ffff0)
(out/'triangle.exe').write_bytes(header+code)
print(out/'triangle.exe')
