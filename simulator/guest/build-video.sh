#!/bin/sh
set -eu
cd /work
SOURCE=/build/sources/ffmpeg-6.1.5
OUT=/build/ffmpeg-c1max-video
mkdir -p "$OUT"
if [ ! -f "$OUT/ffbuild/config.mak" ]; then
    (cd "$OUT" && "$SOURCE/configure" \
        --enable-cross-compile --cross-prefix=mipsel-linux-gnu- --arch=mips --cpu=mips32r2 --target-os=linux \
        --extra-cflags='-O2 -march=mips32r2 -mabi=32 -mhard-float -mfp32 -I/opt/c1sim-openssl/usr/include/mipsel-linux-gnu -I/opt/c1sim-openssl/usr/include' \
        --extra-ldflags='-static -L/opt/c1sim-openssl/usr/lib/mipsel-linux-gnu' --extra-libs='-latomic -ldl -pthread' --pkg-config=false \
        --disable-shared --enable-static --disable-autodetect --disable-everything \
        --disable-programs --disable-doc --disable-debug --disable-asm --enable-network --enable-openssl --enable-version3 \
        --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
        --enable-avformat --enable-avcodec --enable-avutil --enable-swresample \
        --enable-protocol=file,http,https,tcp,tls --enable-demuxer=mov --enable-parser=h264,aac --enable-decoder=h264,aac \
        >/build/c1max-video-config.log 2>&1) || { tail -40 /build/c1max-video-config.log; exit 1; }
fi
make -s -C "$OUT" -j"$(nproc)"
mipsel-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror -march=mips32r2 -mabi=32 -mhard-float -mfp32 -static \
    -I"$SOURCE" -I"$OUT" guest/video.c \
    "$OUT/libavformat/libavformat.a" "$OUT/libavcodec/libavcodec.a" \
    "$OUT/libswresample/libswresample.a" "$OUT/libavutil/libavutil.a" \
    /opt/c1sim-openssl/usr/lib/mipsel-linux-gnu/libssl.a /opt/c1sim-openssl/usr/lib/mipsel-linux-gnu/libcrypto.a \
    -latomic -ldl -lm -pthread -o build/images/c1max-video
