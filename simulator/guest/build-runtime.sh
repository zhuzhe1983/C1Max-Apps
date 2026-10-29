#!/bin/sh
set -eu
cd /work
UP=.cache/upstream/C1Max-Apps
CC=mipsel-linux-gnu-gcc
CXX=mipsel-linux-gnu-g++
FLAGS='-Os -march=mips32r2 -mabi=32 -mhard-float -mfp32'
mkdir -p build/driver build/images build/payload/launcher build/payload/shared build/objects
cp guest/driver/* build/driver/
make -s -C /build/sources/linux-6.6.60 O=/build/linux ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- M=/work/build/driver modules
cp build/driver/c1maxsim.ko build/images/
$CC $FLAGS -std=c11 -Wall -Wextra -Werror -static guest/bridge.c -o build/images/c1max-bridge
$CC $FLAGS -std=c11 -Wall -Wextra -Werror -static guest/wget.c -o build/images/c1max-wget
$CC $FLAGS -std=c11 -Wall -Wextra -Werror -static guest/mplayer.c -o build/images/c1max-mplayer
$CC $FLAGS -std=c11 -Wall -Wextra -Werror -static -pthread guest/host-video.c -lm -o build/images/c1max-host-video
sh guest/build-video.sh
$CC $FLAGS -c "$UP/shared/audio_client.c" -o build/objects/audio_client.o
$CC $FLAGS -I.cache/upstream "$UP/launcher/src/launcher.c" "$UP/launcher/src/typeface.c" build/objects/audio_client.o -static -lm -o build/payload/launcher/c1max-launcher
# nlohmann/json 只含头文件；隔离复制，避免把主机 libc 头加入交叉编译搜索路径。
mkdir -p build/objects/include build/licenses/nlohmann-json
cp -a /usr/include/nlohmann build/objects/include/
cp /usr/share/doc/nlohmann-json3-dev/copyright build/licenses/nlohmann-json/
$CXX $FLAGS -std=c++17 -Ibuild/objects/include "$UP/shared/audio_service.cpp" \
    "$UP/shared/cast_client.cpp" "$UP/shared/output_mode.cpp" build/objects/audio_client.o \
    -static -pthread -o build/payload/shared/c1max-audio
for name in pcm pcm_hw mixer mixer_hw limits; do
    $CC $FLAGS -I"$UP/shared/tinyalsa/include" -c "$UP/shared/tinyalsa/src/$name.c" -o "build/objects/$name.o"
done
$CC $FLAGS -I"$UP/shared/tinyalsa/include" "$UP/shared/volume.c" build/objects/pcm.o build/objects/pcm_hw.o build/objects/mixer.o build/objects/mixer_hw.o build/objects/limits.o -static -o build/payload/shared/c1max-volume
echo 'C1 Max 驱动、桥接和原源码 MIPS 基础运行时已编译。'
