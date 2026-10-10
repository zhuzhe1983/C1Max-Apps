#!/usr/bin/env python3
"""Add the sound bridge to a build copy; never modify the pinned checkout."""
import pathlib
import shutil
import sys

source, target = map(pathlib.Path, sys.argv[1:])
shutil.copytree(source, target, dirs_exist_ok=True)

def replace(text, old, new):
    if text.count(old) != 1:
        raise SystemExit('InfoNES sound hook no longer matches pinned source: ' + old[:70])
    return text.replace(old, new)

p = target/'K6502.cpp'
s = p.read_text()
s = replace(s, '#define CLK(a)   g_wPassedClocks += (a);',
    '#include "apu.hpp"\n#define CLK(a) do { const WORD elapsed=(a); g_wPassedClocks+=elapsed; c1_nes_apu_cycles+=elapsed; } while(0);')
s = replace(s, 'void K6502_Step( WORD wClocks )\n{',
    'void K6502_Step( WORD wClocks )\n{\n  if(c1_nes_apu_irq_pending()) IRQ_REQ;')
p.write_text(s)

p = target/'K6502_rw.h'
s = replace(p.read_text(), '#include "InfoNES_pAPU.h"', '#include "InfoNES_pAPU.h"\n#include "apu.hpp"')
start = s.index('        // APU control\n')
end = s.index('        return byRet;', start) + len('        return byRet;')
s = replace(s, s[start:end], '        return c1_nes_apu_status();')
start = s.index('        case 0x17:  /* 0x4017 */')
end = s.index('          break;', start)
s = replace(s, s[start:end], '        case 0x17:  /* 0x4017 */\n          c1_nes_apu_frame_counter(byData);\n')
p.write_text(s)

p = target/'InfoNES.cpp'
s = p.read_text()
start = s.index('    // Frame IRQ in H-Sync\n')
end = s.index('    // A mapper function in H-Sync', start)
s = replace(s, s[start:end], '    // Frame/DMC IRQs are supplied by the sound bridge in K6502_Step.\n\n')
s = replace(s, '      if ( !APU_Mute )\n        InfoNES_pAPUVsync();', '      InfoNES_pAPUVsync();')
p.write_text(s)
