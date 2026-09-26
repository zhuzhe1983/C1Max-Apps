#!/bin/sh
# Run inside the build container after the normal application build.
set -eu
cd "$(dirname "$0")/../.."
cmake --build .build/mips --target c1max-bilibili-test -j4
qemu-mipsel .build/mips/c1max-bilibili-test
mkdir -p .build/bilibili-tests/include/libavutil
printf '#define AV_HAVE_BIGENDIAN 0\n#define AV_HAVE_FAST_UNALIGNED 0\n' > .build/bilibili-tests/include/libavutil/avconfig.h
gcc -std=c99 -Wall -Wextra -Wno-misleading-indentation -O1 -fsanitize=address,undefined \
    -I.build/bilibili-tests/include -Istreamplayer/vendor/ffmpeg42 \
    bilibili/tests/scaling_test.c -ldl -o .build/bilibili-tests/scaling-test
.build/bilibili-tests/scaling-test
