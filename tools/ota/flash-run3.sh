#!/bin/sh
# Run recovery on our signed package inside a tmpfs chroot.
# A detached supervisor owns the bind mounts until recovery exits,
# then syncs, self-verifies the flashed partition byte-exactly,
# records the result, and only then releases the mounts.
# No rm -rf anywhere; unique mktemp dir.
set -eu
F=$(mktemp -d /tmp/flashroot.XXXXXX)
mkdir -p "$F/bin" "$F/lib" "$F/usr/data" "$F/storage" "$F/dev" "$F/proc" "$F/tmp"

cp /bin/busybox "$F/bin/"
for a in sh dd cp unzip echo sync cat ls mount umount env sha256sum cut wc sleep reboot expr head; do
    ln -sf busybox "$F/bin/$a"
done
cp /usr/sbin/recovery "$F/"
cp -L /lib/ld-linux-mipsn8.so.1 /lib/libc.so.6 /lib/libm.so.6 /lib/libdl.so.2 \
      /lib/libpthread.so.0 /lib/libgcc_s.so.1 /usr/lib/libstdc++.so.6 \
      /usr/lib/libcjson.so.1 /lib/libresolv.so.2 "$F/lib/"

cat > "$F/supervise.sh" <<'EOS'
#!/bin/sh
PATH=/bin:/usr/data/ota_res
LD_LIBRARY_PATH=/lib
unset LD_PRELOAD
/recovery > /storage/flash.log 2>&1
ret=$?
sync
PAY=/storage/update/update001/rootfs.ext4
if [ -f "$PAY" ]; then
    SZ=$(wc -c < "$PAY")
    A=$(dd if=/dev/mmcblk0p8 2>/dev/null | head -c "$SZ" | sha256sum | cut -d' ' -f1)
    B=$(sha256sum "$PAY" | cut -d' ' -f1)
    [ "$A" = "$B" ] && V=VERIFY_PASS || V=VERIFY_FAIL
else
    V=VERIFY_SKIPPED_NO_PAYLOAD
fi
U=ok
for m in /usr/data /proc /dev /tmp; do
    umount "$m" 2>/dev/null || U="$U fail:$m"
done
echo "exit=$ret verify=$V umount=$U" > /storage/flash.done
sync
umount /storage 2>/dev/null
EOS
chmod +x "$F/supervise.sh"

mount --bind /usr/data "$F/usr/data"
mount --bind /storage  "$F/storage"
mount --bind /dev      "$F/dev"
mount --bind /proc     "$F/proc"
mount --bind /tmp      "$F/tmp"

rm -f /usr/data/UPDATE_RET /storage/flash.done
dd if=/dev/zero of=/dev/mmcblk0p3 bs=512 count=1
sync

setsid /bin/busybox chroot "$F" /bin/sh /supervise.sh </dev/null >/dev/null 2>&1 &
echo $! > /storage/flash.pid
sleep 2
if kill -0 "$(cat /storage/flash.pid)" 2>/dev/null; then
    echo FLASH_STARTED
else
    echo FLASH_START_FAILED
    exit 1
fi
