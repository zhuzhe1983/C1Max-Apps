#!/bin/sh
# On-device: run launcher with debug logging and NO injected input for 3s, to
# check whether scroll_y drifts on its own.
cd /storage/launcher || exit 1
killall c1max-launcher 2>/dev/null
sleep 1
C1L_DEBUG=1 C1L_CONFIG=/storage/launcher/apps_safe.txt \
  ./c1max-launcher /dev/fb2 >/storage/launcher/log.txt 2>&1 &
P=$!
sleep 3
dd if=/dev/fb2 bs=1088000 count=1 of=/storage/launcher/cap_noinput.raw 2>/dev/null
kill $P 2>/dev/null
sleep 1
echo "=== LOG (no input) ==="
cat /storage/launcher/log.txt
