#!/bin/bash
# inject-and-sign.sh - take a stock C1 Max update.zip, inject the rootfs
# payload (idempotently, per docs/firmware-ota.md §6), repack and self-sign
# with our c1key.pem so the result flashes with tools/ota/flash-run3.sh.
#
# Target host: the build machine (Debian; zip/unzip/e2fsck/mount/python3/
# openssl, passwordless sudo). Not macOS.
#
# Usage: inject-and-sign.sh <in.zip> <out.zip> <c1key.pem> <c1key.v2.pub>
#
# Safety contract (see docs/firmware-ota.md §4 for why):
#   - all scratch state under one mktemp -d dir;
#   - EXIT trap umounts the loop mount BEFORE any cleanup;
#   - never rm -rf anything that may still hold a mountpoint - the mount
#     dir itself is removed with rmdir only, after a successful umount.
set -euo pipefail

die() { echo "inject-and-sign: ERROR: $*" >&2; exit 1; }

[ $# -eq 4 ] || {
    echo "usage: $0 <in.zip> <out.zip> <c1key.pem> <c1key.v2.pub>" >&2
    exit 2
}

IN=$1; OUT=$2; PEM=$3; V2PUB=$4

[ -f "$IN" ]    || die "input zip not found: $IN"
[ -f "$PEM" ]   || die "private key not found: $PEM"
[ -f "$V2PUB" ] || die "v2 public key not found: $V2PUB"

# Sibling tools shipped in tools/ota/ of the repo.
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
C1SIGN=$SCRIPT_DIR/c1sign.py
QUAR=$SCRIPT_DIR/ota-quarantine.sh
[ -f "$C1SIGN" ] || die "c1sign.py not found next to this script ($SCRIPT_DIR)"
[ -f "$QUAR" ]   || die "ota-quarantine.sh not found next to this script ($SCRIPT_DIR)"

for c in unzip zip e2fsck blkid file python3 openssl sudo mountpoint stat; do
    command -v "$c" >/dev/null || die "missing command: $c"
done
sudo -n true 2>/dev/null || die "sudo is not passwordless on this host"

# Absolute paths before we cd around.
abspath() { # $1 must exist as file; prints absolute path
    local d b
    d=$(cd "$(dirname "$1")" && pwd); b=$(basename "$1")
    echo "$d/$b"
}
IN=$(abspath "$IN"); PEM=$(abspath "$PEM"); V2PUB=$(abspath "$V2PUB")
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
[ "$IN" != "$OUT" ] || die "input and output must differ"

WORK=$(mktemp -d /tmp/inject-sign.XXXXXX)
MNT=$WORK/mnt
PKG=$WORK/pkg
mkdir -p "$MNT" "$PKG"

cleanup() {
    rc=$?
    set +e
    if mountpoint -q "$MNT"; then
        if sudo umount "$MNT"; then
            rmdir "$MNT"
        else
            echo "inject-and-sign: ERROR: umount of $MNT failed; leaving $WORK in place" >&2
            exit 1
        fi
    fi
    # The only mountpoint under $WORK is $MNT and it is gone now (umount +
    # rmdir above), so rm -rf $WORK cannot touch mounted content.
    rmdir "$MNT" 2>/dev/null
    rm -rf "$WORK"
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 129' TERM

# --- 1. unpack -------------------------------------------------------------
rc=0
unzip -q "$IN" -d "$PKG" || rc=$?
[ "$rc" -le 1 ] || die "unzip failed (rc=$rc)"   # 1 = warning (signed-comment tail)

[ -f "$PKG/update/update000/update.xml" ] || die "update/update000/update.xml missing - not the expected update.zip layout"
[ -d "$PKG/update/update001" ] || die "update/update001/ missing - not the expected update.zip layout"

# --- 2. locate the rootfs payload in update001/ ----------------------------
# Candidate: regular file, >= 400 MiB, ext-family filesystem. Exactly one
# must match; zero or several is an error - never guess.
MIN=$((400 * 1024 * 1024))
IMG=""
for f in "$PKG"/update/update001/*; do
    [ -f "$f" ] || continue
    sz=$(stat -c %s "$f")
    [ "$sz" -ge "$MIN" ] || continue
    t=$(blkid -p -o value -s TYPE "$f" 2>/dev/null || true)
    case $t in
        ext2|ext3|ext4) ;;
        *) file -b "$f" | grep -Eq 'ext[234] filesystem' || continue ;;
    esac
    [ -z "$IMG" ] || die "multiple payload candidates: $IMG and $f - refusing to guess"
    IMG=$f
done
[ -n "$IMG" ] || die "no ext-family payload >= 400 MiB found under update/update001/"
echo "payload: $(basename "$IMG") ($(stat -c %s "$IMG") bytes)"

rc=0
e2fsck -fy "$IMG" || rc=$?
[ "$rc" -le 1 ] || die "e2fsck failed (rc=$rc)"

sudo mount -o loop "$IMG" "$MNT" || die "loop mount failed"
echo "mounted: $MNT"

# --- 3. inject /etc/init.rc (all steps idempotent) --------------------------
rc=0
python3 - "$MNT/etc/init.rc" "$WORK/init.rc.new" <<'PYEOF' || rc=$?
import sys

src, dst = sys.argv[1], sys.argv[2]
lines = open(src).read().splitlines()
notes = []

def insert_after(anchor, newlines):
    """Insert each missing line of newlines right after the (unique) anchor
    line. Anchor missing => the layout is not what we know => hard error."""
    missing = [l for l in newlines if l not in lines]
    if not missing:
        notes.append('SKIP  after %r (all present)' % anchor.strip())
        return
    if anchor not in lines:
        raise SystemExit('anchor line not found in init.rc: %r' % anchor)
    i = lines.index(anchor)
    lines[i + 1:i + 1] = missing
    notes.append('ADD   %d line(s) after %r' % (len(missing), anchor.strip()))

def append_block(markers, block_lines, label):
    """Append a block unless its marker comment or characteristic service
    line is already present."""
    if any(m in lines for m in markers):
        notes.append('SKIP  %s (already present)' % label)
        return
    lines.extend([''] + block_lines)
    notes.append('ADD   %s' % label)

# a. on-init properties, right after `start logd`
insert_after('   start logd', [
    '   setprop user.usb.config adb',
    '   setprop service.adb.tcp.port 5555',
    '   setprop sys.backlight.lock 1',
])

# b. c1desktop launcher autostart (marker-wrapped)
append_block(
    ['# BEGIN C1MAX DESKTOP',
     'service c1desktop /storage/apps/current/launcher/desktop-service.sh'],
    ['# BEGIN C1MAX DESKTOP',
     'on property:init.svc.smartUI=running',
     '    start c1desktop',
     '',
     'service c1desktop /storage/apps/current/launcher/desktop-service.sh',
     '    disabled',
     '    oneshot',
     '# END C1MAX DESKTOP'],
    'c1desktop block')

# c. c1apps-hotkey block - byte-identical to BLOCK in tools/hotkey_service.py
append_block(
    ['# BEGIN C1MAX APPS HOTKEY',
     'service c1apps-hotkey /storage/apps/current/shared/c1max-hotkey'],
    ['# BEGIN C1MAX APPS HOTKEY',
     'on property:init.svc.smartUI=running',
     '    start c1apps-hotkey',
     '',
     'service c1apps-hotkey /storage/apps/current/shared/c1max-hotkey',
     '    class main',
     '    disabled',
     '# END C1MAX APPS HOTKEY'],
    'c1apps-hotkey block')

# d. otaquarantine: trigger in `on userboot` right after `start smartUI`,
#    plus the service block
insert_after('   start smartUI', ['   start otaquarantine'])
append_block(
    ['service otaquarantine /etc/ota-quarantine.sh'],
    ['service otaquarantine /etc/ota-quarantine.sh',
     '    class core',
     '    disabled',
     '    oneshot'],
    'otaquarantine service')

# e. c1telnet: trigger in `on boot` right after `start network`, plus service
insert_after('   start network', ['   start c1telnet'])
append_block(
    ['service c1telnet /usr/sbin/telnetd -p 2323 -l /bin/sh'],
    ['service c1telnet /usr/sbin/telnetd -p 2323 -l /bin/sh',
     '    class core',
     '    disabled'],
    'c1telnet service')

changed = any(n.startswith('ADD') for n in notes)
for n in notes:
    print('init.rc:', n)
if changed:
    open(dst, 'w').write('\n'.join(lines) + '\n')
sys.exit(0 if changed else 10)
PYEOF
case $rc in
    0)
        sudo install -m 0644 -o root -g root "$WORK/init.rc.new" "$MNT/etc/init.rc"
        echo "init.rc: installed updated file"
        ;;
    10)
        echo "init.rc: all blocks already present (idempotent, file untouched)"
        ;;
    *) die "init.rc transform failed (rc=$rc)" ;;
esac

# --- 4. /etc/ota-quarantine.sh ---------------------------------------------
qmode=$(stat -c %a "$MNT/etc/ota-quarantine.sh" 2>/dev/null || echo none)
if [ "$qmode" = 755 ] && cmp -s "$QUAR" "$MNT/etc/ota-quarantine.sh"; then
    echo "ota-quarantine.sh: already installed (idempotent skip)"
else
    sudo install -m 0755 -o root -g root "$QUAR" "$MNT/etc/ota-quarantine.sh"
    echo "ota-quarantine.sh: installed (0755 root:root)"
fi

# --- 5. /etc/ota_res/key/key.pub --------------------------------------------
KEYDIR=$MNT/etc/ota_res/key
[ -d "$KEYDIR" ] || die "$KEYDIR missing in payload"
if cmp -s "$V2PUB" "$KEYDIR/key.pub"; then
    echo "key.pub: already ours (idempotent skip, vendorbak untouched)"
else
    if [ ! -e "$KEYDIR/key.pub.vendorbak" ]; then
        sudo install -m 0644 -o root -g root "$KEYDIR/key.pub" "$KEYDIR/key.pub.vendorbak"
        echo "key.pub: vendor key backed up to key.pub.vendorbak"
    else
        echo "key.pub: vendorbak already exists, not overwriting"
    fi
    # tee keeps the existing inode/mode (image ships 0666 root:root)
    sudo tee "$KEYDIR/key.pub" < "$V2PUB" > /dev/null
    cmp -s "$V2PUB" "$KEYDIR/key.pub" || die "key.pub replacement verify failed"
    echo "key.pub: replaced with our v2 public key"
fi

# --- 6. /usr/bin/usbconfig.sh typo (let overtime= -> overtimer=) ------------
USBCFG=$MNT/usr/bin/usbconfig.sh
if [ -f "$USBCFG" ]; then
    if grep -q 'let overtime=overtime-1' "$USBCFG"; then
        sudo sed -i 's/let overtime=overtime-1/let overtimer=overtimer-1/g' "$USBCFG"
        echo "usbconfig.sh: fixed 'let overtime=overtime-1' typo"
    else
        echo "usbconfig.sh: no typo present (idempotent skip)"
    fi
else
    echo "usbconfig.sh: not present in payload, skipped"
fi

# --- 7. umount, repack ------------------------------------------------------
sudo umount "$MNT" || die "umount failed"
rmdir "$MNT"
echo "umounted"

UNSIGNED=$WORK/unsigned.zip
( cd "$PKG" && zip -1 -X -q -r "$UNSIGNED" update )
[ -s "$UNSIGNED" ] || die "repack produced empty zip"

# --- 8. sign + verify -------------------------------------------------------
python3 - "$C1SIGN" "$PEM" "$V2PUB" "$UNSIGNED" "$WORK/signed.zip" <<'PYEOF'
import os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(sys.argv[1])))
from c1sign import sign_zip, verify_zip, load_v2_key

pem, v2pub, zin, zout = sys.argv[2:6]

txt = subprocess.check_output(
    ['openssl', 'rsa', '-in', pem, '-text', '-noout'],
    stderr=subprocess.DEVNULL).decode()

def grab(label):
    m = re.search(label + r':\s*((?:\s*[0-9a-fA-F]{2}:?)+)', txt, re.I)
    if not m:
        raise SystemExit('cannot parse %s from openssl rsa -text output' % label)
    return int(re.sub(r'[\s:]', '', m.group(1)), 16)

n = grab('modulus')
d = grab('privateExponent')

n_pub, e_pub = load_v2_key(v2pub)
if n_pub != n:
    raise SystemExit('c1key.pem modulus does not match the v2 public key')
if e_pub != 65537:
    raise SystemExit('v2 key exponent is not 65537')

digest = sign_zip(zin, zout, d, n)
ok, info = verify_zip(zout, n_pub, e_pub)
print('sign: sha1=%s' % digest)
print('VERIFY', 'PASS' if ok else 'FAIL', info)
sys.exit(0 if ok else 1)
PYEOF

mv -f "$WORK/signed.zip" "$OUT"

echo
echo "output: $OUT ($(stat -c %s "$OUT") bytes)"
echo "next:   adb push '$OUT' /storage/update/update.zip"
echo "        然后在设备上跑 flash-run3.sh（tools/ota/flash-run3.sh）刷入"
