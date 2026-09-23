#!/bin/sh
# On-device: prove injected touch drives scrolling deterministically.
cd /storage/launcher || exit 1
killall c1max-launcher 2>/dev/null
sleep 1
C1L_DEBUG=1 C1L_CONFIG=/storage/launcher/apps_safe.txt \
  ./c1max-launcher /dev/fb2 >/storage/launcher/log.txt 2>&1 &
P=$!
sleep 1
dd if=/dev/fb2 bs=1088000 count=1 of=/storage/launcher/cap_top.raw 2>/dev/null
echo "-- inject scroll ABS_X 300->40 (swipe up) --" >>/storage/launcher/log.txt
./inject 300 399 40 399 16 20
sleep 1
dd if=/dev/fb2 bs=1088000 count=1 of=/storage/launcher/cap_scrolled.raw 2>/dev/null
kill $P 2>/dev/null
sleep 1
echo "=== LOG ==="
cat /storage/launcher/log.txt
