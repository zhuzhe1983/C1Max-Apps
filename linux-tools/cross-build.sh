#!/usr/bin/env bash
set -euo pipefail
cd /work
export LC_ALL=C TZ=UTC
export SOURCE_DATE_EPOCH=1789689600
export ARFLAGS=crD
export CC=mipsel-linux-gnu-gcc CXX=mipsel-linux-gnu-g++ AR=mipsel-linux-gnu-ar
export RANLIB=mipsel-linux-gnu-ranlib STRIP=mipsel-linux-gnu-strip
export BUILD_CC=gcc CC_FOR_BUILD=gcc
export CFLAGS='-Os -march=mips32r2 -mabi=32 -ffunction-sections -fdata-sections -ffile-prefix-map=/work=.'
export CXXFLAGS="$CFLAGS"
export LDFLAGS='-static -Wl,--gc-sections -Wl,--build-id=none'
export JOBS="${JOBS:-4}"
BUILD=/work/.build
PREFIX=/storage/apps/current/linux-tools
STAGE="$BUILD/sysroot"
OUT="$BUILD/linux-tools"
mkdir -p "$BUILD/logs" "$BUILD/build" "$OUT/bin" "$OUT/share/terminfo"
exec 9>"$BUILD/build.lock"
flock 9

# A source or recipe change invalidates every build; the source archives stay cached.
FINGERPRINT=$(cat sources.json cross-build.sh Dockerfile | sha256sum | cut -d' ' -f1)
FINGERPRINT="$FINGERPRINT:$(mipsel-linux-gnu-gcc -dumpfullversion):$(dpkg-query -W -f='${Version}' libc6-dev-mipsel-cross)"
if [ "$(cat "$BUILD/fingerprint" 2>/dev/null || true)" != "$FINGERPRINT" ]; then
    python3 - <<'PY'
import json, pathlib, shutil, tarfile
root=pathlib.Path('/work'); build=root/'.build'
for name in ('src', 'build', 'sysroot', 'linux-tools'):
    path=build/name
    if path.exists(): shutil.rmtree(path)
    path.mkdir()
for item in json.loads((root/'sources.json').read_text())['sources']:
    with tarfile.open(build/'downloads'/item['url'].rsplit('/',1)[-1]) as archive:
        destination=build/'src'
        for member in archive.getmembers():
            target=(destination/member.name).resolve()
            target.relative_to(destination)
            if member.issym() or member.islnk():
                base=target.parent if member.issym() else destination
                (base/member.linkname).resolve().relative_to(destination)
            elif not (member.isfile() or member.isdir()):
                raise ValueError('Unsupported archive member: '+member.name)
            member.mode &= 0o777
        archive.extractall(destination)
PY
    for PATCH in "$BUILD"/downloads/bash53-*; do
        patch --batch --forward -d "$BUILD/src/bash-5.3" -p0 < "$PATCH" >/dev/null
    done
    printf '%s' "$FINGERPRINT" > "$BUILD/fingerprint"
fi
mkdir -p "$OUT/bin" "$OUT/share/terminfo"
HOST=$(gcc -dumpmachine)

build_ncurses() {
    mkdir -p "$BUILD/build/ncurses"; cd "$BUILD/build/ncurses"
    # The minimal builder has no non-root login users. Override that host-only
    # heuristic so the target's root terminal can honor TERMINFO at runtime.
    cf_cv_ar_flags= cf_cv_multiuser=yes "$BUILD/src/ncurses-6.6/configure" --build="$HOST" --host=mipsel-linux-gnu \
        --prefix=/usr --without-shared --without-debug --without-ada \
        --without-cxx --without-cxx-binding --without-progs --without-tests \
        --without-manpages --without-gpm --enable-widec --enable-overwrite --enable-root-environ \
        --disable-db-install --with-default-terminfo-dir="$PREFIX/share/terminfo" \
        --with-terminfo-dirs="$PREFIX/share/terminfo:/etc/terminfo:/usr/share/terminfo"
    make -j"$JOBS"
    make DESTDIR="$STAGE" install
}
step() {
    local name=$1; shift
    if [ -f "$BUILD/build/$name.done" ]; then printf '%s: cached\n' "$name"; return; fi
    printf '%s: building (log .build/logs/%s.log)\n' "$name" "$name"
    # Keep the subshell outside an if condition so errexit remains active inside
    # each build function (a failed configure must not run make on old files).
    ( "$@" ) >"$BUILD/logs/$name.log" 2>&1
    touch "$BUILD/build/$name.done"
}
step ncurses build_ncurses
export CPPFLAGS="-I$STAGE/usr/include"
export LDFLAGS="$LDFLAGS -L$STAGE/usr/lib"
export PKG_CONFIG_LIBDIR="$STAGE/usr/lib/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR="$STAGE"

build_bash() {
    mkdir -p "$BUILD/build/bash"; cd "$BUILD/build/bash"
    "$BUILD/src/bash-5.3/configure" --build="$HOST" --host=mipsel-linux-gnu \
        --prefix="$PREFIX" --disable-nls --enable-static-link --without-bash-malloc \
        --with-curses --disable-profiling
    make -j"$JOBS"
    install -m755 bash "$OUT/bin/bash"
}
build_less() {
    mkdir -p "$BUILD/build/less"; cd "$BUILD/build/less"
    "$BUILD/src/less-710/configure" --build="$HOST" --host=mipsel-linux-gnu --prefix="$PREFIX"
    make -j"$JOBS"
    install -m755 less "$OUT/bin/less"
}
build_nano() {
    mkdir -p "$BUILD/build/nano"; cd "$BUILD/build/nano"
    "$BUILD/src/nano-9.2/configure" --build="$HOST" --host=mipsel-linux-gnu \
        --prefix="$PREFIX" --disable-nls --disable-libmagic --disable-speller --enable-utf8
    make -j"$JOBS"
    install -m755 src/nano "$OUT/bin/nano"
}
build_dropbear() {
    # The release makefiles build bundled crypto in the source tree.
    cd "$BUILD/src/dropbear-2026.94"
    # No server is built. Shared headers otherwise require libcrypt for the
    # unused server-password path; client password authentication stays enabled.
    printf '#define DROPBEAR_SVR_PASSWORD_AUTH 0\n' > localoptions.h
    ./configure --build="$HOST" --host=mipsel-linux-gnu --prefix="$PREFIX" \
        --enable-static --enable-bundled-libtom --disable-zlib --disable-syslog \
        --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx
    make -j"$JOBS" PROGRAMS='dbclient dropbearkey' STATIC=1
    install -m755 dbclient dropbearkey "$OUT/bin/"
}
step bash build_bash
step less build_less
step nano build_nano
step dropbear build_dropbear
for BIN in "$OUT"/bin/*; do "$STRIP" --strip-unneeded "$BIN"; done

# Compile only the terminal types advertised by the C1Max terminal app.
tic -x -e xterm-256color,vt100 -o "$OUT/share/terminfo" "$BUILD/src/ncurses-6.6/misc/terminfo.src"
cp -R /work/licenses "$OUT/share/"
cp /work/sources.json "$OUT/share/sources.json"
mkdir -p "$OUT/share/licenses/toolchain"
cp /usr/share/doc/libc6-dev-mipsel-cross/copyright "$OUT/share/licenses/toolchain/glibc-copyright"
cp /usr/share/common-licenses/LGPL-2.1 "$OUT/share/licenses/toolchain/"
cp /usr/share/common-licenses/GPL-3 "$OUT/share/licenses/toolchain/"
cp /usr/share/doc/libgcc-12-dev-mipsel-cross/copyright "$OUT/share/licenses/toolchain/gcc-copyright"
dpkg-query -W gcc-mipsel-linux-gnu gcc-12-mipsel-linux-gnu libc6-dev-mipsel-cross libgcc-12-dev-mipsel-cross ncurses-bin > "$OUT/share/toolchain.txt"
cd /work
python3 verify.py
