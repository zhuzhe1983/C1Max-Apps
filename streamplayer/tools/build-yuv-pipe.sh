#!/bin/sh
# Run inside c1max-apps-builder:bookworm, or another matching cross-toolchain.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
OUT=${1:?usage: build-yuv-pipe.sh /absolute/c1max-yuv-pipe.so}
exec python3 - "$ROOT" "$OUT" <<'PY'
import pathlib, subprocess, sys, tempfile
root=pathlib.Path(sys.argv[1]); output=pathlib.Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix='c1-yuv-build-') as temp:
    headers=pathlib.Path(temp)
    (headers/'gnu').mkdir(); (headers/'libavutil').mkdir()
    # stubs only declares unimplemented libc entry points, not float layouts.
    # This shim uses no floating-point libc interface and links no host libc.
    (headers/'gnu/stubs-o32_hard_2008.h').symlink_to('/usr/mipsel-linux-gnu/include/gnu/stubs-o32_hard.h')
    (headers/'libavutil/avconfig.h').write_text('#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 0\n')
    subprocess.run(['mipsel-linux-gnu-gcc','-std=c99','-Os','-Wall','-Wextra','-Werror',
        '-march=mips32r2','-mabi=32','-mfp64','-mnan=2008','-fPIC','-fno-stack-protector',
        '-fno-builtin','-U_FORTIFY_SOURCE','-D_FORTIFY_SOURCE=0','-shared','-nostdlib',
        '-Wl,-soname,c1max-yuv-pipe.so','-I'+str(headers),'-I'+str(root/'vendor/ffmpeg42'),
        '-o',str(output),str(root/'src/yuv_pipe.c')],check=True)
    subprocess.run(['mipsel-linux-gnu-readelf','-h','-A','-V',str(output)],check=True)
    dynamic=subprocess.check_output(['mipsel-linux-gnu-readelf','-d',str(output)]).decode()
    assert 'NEEDED' not in dynamic
    print('Built',output,output.stat().st_size,'bytes; no DT_NEEDED')
PY
