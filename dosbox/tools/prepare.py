#!/usr/bin/env python3
"""Copy the pinned upstream tree with minimal, deterministic C1Max changes."""
import pathlib, sys
source, dest = map(pathlib.Path, sys.argv[1:])
for path in source.rglob('*'):
    relative = path.relative_to(source)
    if any(part in {'.git', 'images', 'build'} for part in relative.parts) or not path.is_file():
        continue
    data = path.read_bytes()
    if str(relative) == 'include/config.h':
        # X2000/MIPS32r2 requires aligned loads; x86 guest data does not.
        # The upstream MIPSel default permits native unaligned dereferences.
        needle = b'#elif defined(__mips__) && defined(__MIPSEL__)\n#define C_DYNREC 1\n#define C_UNALIGNED_MEMORY 1'
        assert needle in data
        data = data.replace(needle, b'#elif defined(__mips__) && defined(__MIPSEL__)\n#define C_DYNREC 1\n/* C1Max: use alignment-safe guest memory access. */')
        for feature in ['C_DBP_SUPPORT_MIDI_MT32', 'C_DBP_SUPPORT_MIDI_SC55', 'C_DBP_SUPPORT_MIDI_TSF']:
            data = data.replace(('#define '+feature).encode(), ('#undef '+feature).encode())
    if str(relative) == 'src/gui/midi.cpp':
        needle = b'if (midi.handler != &Midi_tsf && midi.handler != &Midi_mt32 && midi.handler != &Midi_sc55)'
        assert needle in data
        data = data.replace(needle, b'if (true) /* External synthesizers are not compiled in this profile. */')
        data += b'\n// No SoundFont backend in the C1Max profile.\nbool MIDI_TSF_SwitchSF(const char*) { return false; }\n'
    target = dest / relative
    if not target.is_file() or target.read_bytes() != data:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
