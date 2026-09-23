#!/usr/bin/env python3
"""Link a throwaway MIPS binary against the existing shared LVGL archive.

Run inside c1max-apps-builder:bookworm with the apps tree mounted at /work:
  python3 terminal/tools/check_mips.py
Does not regenerate or mutate the integration build.
"""
import pathlib
import subprocess
import tempfile

terminal = pathlib.Path(__file__).resolve().parents[1]
apps = terminal.parent
lvgl = apps / '.build/mips/.deps/lvgl/lib/liblvgl.a'
if not lvgl.is_file():
    raise SystemExit('Build the shared LVGL archive first with apps/tools/build.sh')
flags = ['-Os', '-march=mips32r2', '-mabi=32', '-ffunction-sections', '-fdata-sections']
includes = ['-I' + str(terminal/'vendor/libvterm/include'), '-I' + str(apps/'shared'),
            '-I' + str(apps/'.deps/lvgl'), '-DLV_CONF_INCLUDE_SIMPLE']
with tempfile.TemporaryDirectory(prefix='c1max-terminal-mips-') as tmp:
    directory = pathlib.Path(tmp)
    objects = []
    for source in sorted((terminal/'vendor/libvterm/src').glob('*.c')):
        obj = directory/(source.stem+'.o')
        subprocess.run(['mipsel-linux-gnu-gcc', '-std=c99'] + flags + includes +
                       ['-c', str(source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    sources = list((terminal/'src').glob('*.cpp')) + [apps/'shared/display.cpp', apps/'shared/keyboard.cpp']
    binary = directory/'c1max-terminal'
    subprocess.run(['mipsel-linux-gnu-g++', '-std=c++17', '-static', '-Wl,--gc-sections', '-s'] +
                   flags + includes + [str(p) for p in sources] + objects +
                   [str(lvgl), '-lm', '-o', str(binary)], check=True)
    subprocess.run(['mipsel-linux-gnu-readelf', '-h', str(binary)], check=True)
    print('Static MIPS terminal linked:', binary.stat().st_size, 'bytes')
