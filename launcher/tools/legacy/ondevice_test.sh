#!/bin/sh
# Runs ON THE DEVICE. Starts the launcher, captures the grid, injects a scroll
# swipe, captures again, injects a tap, then stops. All framebuffer grabs go to
# /storage/launcher/*.raw for the host to pull afterwards (single adb session).
cd /storage/launcher || exit 1
killall c1max-launcher 2>/dev/null
sleep 1
./c1max-launcher /dev/fb2 >log.txt 2>&1 &
P=$!
sleep 2                                   # draw grid, pass 600ms settle
dd if=/dev/fb2 bs=1088000 count=1 of=cap_grid.raw 2>/dev/null

# vertical scroll: finger from dy~300 up to dy~60 (native ABS_X 300->60),
# ABS_Y fixed 399 (logical dx=400, mid screen)
./inject 300 399 60 399 14 18
sleep 1                                    # let inertia settle
dd if=/dev/fb2 bs=1088000 count=1 of=cap_scroll.raw 2>/dev/null

# tap a tile: logical (dx=300,dy=110) -> ABS_X=110, ABS_Y=799-300=499
# (after scroll this is a lower-row /bin/true app: instant exit -> redraw)
./inject 110 499 110 499 1 40
sleep 1
dd if=/dev/fb2 bs=1088000 count=1 of=cap_after_tap.raw 2>/dev/null

kill $P 2>/dev/null
sleep 1
echo "=== log ==="
cat log.txt
echo "=== done ==="
