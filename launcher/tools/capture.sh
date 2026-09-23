#!/usr/bin/env bash
# Capture current C1Max framebuffer without opening another GUI.
set -euo pipefail
: "${ANDROID_SERIAL:?Set ANDROID_SERIAL to the target C1Max serial}"
OUT="${1:-shot.png}"
TMP="$(mktemp -d)"
trap 'rm -f "$TMP/frame.raw"; rmdir "$TMP"' EXIT
CAPTURE_LOG="$(adb -s "$ANDROID_SERIAL" shell '/storage/apps/current/shared/c1max-capture /storage/apps/data/launcher/screen.raw')"
printf '%s\n' "$CAPTURE_LOG"
# This old adbd can return success even when its shell command failed. Require
# the helper's post-rename marker before pulling, so an old image cannot pass.
if ! printf '%s\n' "$CAPTURE_LOG" | grep -q '^CAPTURED bytes=1088000 '; then
    echo 'Device capture failed or helper is outdated; refusing an old screenshot' >&2
    exit 1
fi
adb -s "$ANDROID_SERIAL" pull /storage/apps/data/launcher/screen.raw "$TMP/frame.raw" >/dev/null
[ "$(wc -c < "$TMP/frame.raw" | tr -d ' ')" = 1088000 ] || { echo 'Incomplete framebuffer capture; retry' >&2; exit 1; }
ffmpeg -y -loglevel error -f rawvideo -pixel_format bgra -video_size 340x800 -i "$TMP/frame.raw" -frames:v 1 -vf transpose=1 "$OUT"
