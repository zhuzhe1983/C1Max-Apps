#!/bin/sh
set -eu
cd /work
mkdir -p /build/sources /build/linux /build/busybox build/images
if [ ! -d /build/sources/linux-6.6.60 ]; then
    tar -xf .cache/sources/linux-6.6.60.tar.xz -C /build/sources
fi
if [ ! -d /build/sources/busybox-1.37.0 ]; then
    tar -xf .cache/sources/busybox-1.37.0.tar.bz2 -C /build/sources
fi
KERNEL=/build/sources/linux-6.6.60
KOUT=/build/linux
make -s -C "$KERNEL" O="$KOUT" ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- KCONFIG_ALLCONFIG=/work/guest/linux.config allnoconfig
python3 - <<'PY'
from pathlib import Path
expected = [line for line in Path('/work/guest/linux.config').read_text().splitlines() if line.startswith('CONFIG_')]
actual = set(Path('/build/linux/.config').read_text().splitlines())
missing = [line for line in expected if line not in actual]
if missing: raise SystemExit('内核配置依赖未满足：' + ', '.join(missing))
PY
make -s -C "$KERNEL" O="$KOUT" ARCH=mips CROSS_COMPILE=mipsel-linux-gnu- -j"$(nproc)" vmlinux modules
BUSYBOX=/build/sources/busybox-1.37.0
make -s -C "$BUSYBOX" O=/build/busybox allnoconfig >/dev/null
python3 - <<'PY'
from pathlib import Path
path = Path('/build/busybox/.config')
options = dict(line.split('=', 1) for line in Path('/work/guest/busybox.config').read_text().splitlines() if line.startswith('CONFIG_'))
lines = []
for line in path.read_text().splitlines():
    name = line.split('=', 1)[0] if line.startswith('CONFIG_') else line[2:].split(' ', 1)[0] if line.startswith('# CONFIG_') else ''
    if name in options:
        lines.append(name + '=' + options.pop(name))
    else:
        lines.append(line)
lines += [name + '=' + value for name, value in options.items()]
path.write_text('\n'.join(lines) + '\n')
PY
yes '' | make -s -C "$BUSYBOX" O=/build/busybox oldconfig >/dev/null
python3 - <<'PY'
from pathlib import Path
actual = set(Path('/build/busybox/.config').read_text().splitlines())
missing = [line for line in Path('/work/guest/busybox.config').read_text().splitlines() if line.startswith('CONFIG_') and line not in actual]
if missing: raise SystemExit('BusyBox 配置依赖未满足：' + ', '.join(missing))
PY
make -s -C "$BUSYBOX" O=/build/busybox CROSS_COMPILE=mipsel-linux-gnu- -j"$(nproc)"
mkdir -p build/licenses/linux build/licenses/busybox
cp "$KERNEL/COPYING" "$KERNEL/LICENSES/preferred/GPL-2.0" build/licenses/linux/
cp "$BUSYBOX/LICENSE" build/licenses/busybox/
cp "$KOUT/vmlinux" build/images/vmlinux
cp /build/busybox/busybox build/images/busybox
if [ ! -d /build/sources/curl-8.11.0 ]; then
    tar -xf .cache/sources/curl-8.11.0.tar.xz -C /build/sources
fi
mkdir -p /build/curl
if [ ! -f /build/curl/Makefile ]; then
    (cd /build/curl && CC=mipsel-linux-gnu-gcc CFLAGS='-Os -march=mips32r2 -mabi=32 -mhard-float -mfp32' LDFLAGS=-static \
        /build/sources/curl-8.11.0/configure --host=mipsel-linux-gnu --build="$(gcc -dumpmachine)" \
        --disable-shared --enable-static --disable-threaded-resolver --without-ssl --without-zlib \
        --without-brotli --without-zstd --without-libpsl --without-libidn2 --without-librtmp --without-nghttp2 \
        --disable-ldap --disable-ldaps >/build/curl-config.log 2>&1) || { tail -40 /build/curl-config.log; exit 1; }
fi
make -s -C /build/curl -j"$(nproc)" LDFLAGS=-all-static
cp /build/curl/src/curl build/images/curl
if [ ! -d /build/sources/ffmpeg-6.1.5 ]; then
    tar -xf .cache/sources/ffmpeg-6.1.5.tar.xz -C /build/sources
fi
python3 guest/ffmpeg_tls.py /build/sources/ffmpeg-6.1.5
mkdir -p /build/ffmpeg-online
if [ ! -f /build/ffmpeg-online/ffbuild/config.mak ]; then
    (cd /build/ffmpeg-online && /build/sources/ffmpeg-6.1.5/configure \
        --enable-cross-compile --cross-prefix=mipsel-linux-gnu- --arch=mips --cpu=mips32r2 --target-os=linux \
        --extra-cflags='-Os -march=mips32r2 -mabi=32 -mhard-float -mfp32 -I/opt/c1sim-openssl/usr/include/mipsel-linux-gnu -I/opt/c1sim-openssl/usr/include' \
        --extra-ldflags='-static -L/opt/c1sim-openssl/usr/lib/mipsel-linux-gnu' --extra-libs='-latomic -ldl -pthread' --pkg-config=false \
        --disable-shared --enable-static --disable-autodetect --disable-everything \
        --disable-programs --disable-doc --disable-debug --disable-asm --enable-network --enable-openssl --enable-version3 \
        --disable-avdevice --disable-avfilter --disable-swscale --disable-postproc \
        --enable-avformat --enable-avcodec --enable-avutil --enable-swresample \
        --enable-protocol=file,http,https,tcp,tls --enable-demuxer=mp3,flac,mov,aac --enable-parser=mpegaudio,flac,aac \
        --enable-decoder=mp3,mp3float,flac,aac >/build/ffmpeg-config.log 2>&1) || { tail -40 /build/ffmpeg-config.log; exit 1; }
fi
make -s -C /build/ffmpeg-online -j"$(nproc)"
mkdir -p build/licenses/ffmpeg build/licenses/openssl build/licenses/ca-certificates
cp /build/sources/ffmpeg-6.1.5/COPYING.LGPLv2.1 /build/sources/ffmpeg-6.1.5/COPYING.LGPLv3 /build/sources/ffmpeg-6.1.5/LICENSE.md build/licenses/ffmpeg/
cp /opt/c1sim-openssl/usr/share/doc/libssl-dev/copyright build/licenses/openssl/
cp /usr/share/doc/ca-certificates/copyright build/licenses/ca-certificates/
cp /opt/c1sim-openssl/version build/images/openssl-version
cp /etc/ssl/certs/ca-certificates.crt build/images/ca-certificates.crt
mipsel-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror -march=mips32r2 -mabi=32 -mhard-float -mfp32 -static guest/audio.c -o build/images/c1sim-audio
mipsel-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror -march=mips32r2 -mabi=32 -mhard-float -mfp32 -static \
    -I/build/sources/ffmpeg-6.1.5 -I/build/ffmpeg-online guest/ffplay.c \
    /build/ffmpeg-online/libavformat/libavformat.a /build/ffmpeg-online/libavcodec/libavcodec.a \
    /build/ffmpeg-online/libswresample/libswresample.a /build/ffmpeg-online/libavutil/libavutil.a \
    /opt/c1sim-openssl/usr/lib/mipsel-linux-gnu/libssl.a /opt/c1sim-openssl/usr/lib/mipsel-linux-gnu/libcrypto.a \
    -latomic -ldl -lm -pthread -o build/images/ffplay
sh guest/build-runtime.sh
python3 scripts/pack_guest.py
echo 'C1 Max MIPS Linux、设备适配与原始应用运行时已构建。'
