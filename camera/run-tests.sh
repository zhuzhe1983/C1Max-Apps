#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TEST_DIR="$(mktemp -d /tmp/c1max-camera-test.XXXXXX)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Wno-missing-field-initializers -Wno-deprecated-declarations \
    -fsanitize=address,undefined -fno-sanitize-recover=all -g \
    -I"$ROOT/.deps/lvgl/src/libs/gltf/stb_image" \
    "$ROOT/camera/src/test.cpp" "$ROOT/camera/src/photo.cpp" "$ROOT/camera/src/filter.cpp" "$ROOT/camera/src/album.cpp" "$ROOT/camera/src/frame_art.cpp" -o "$TEST_DIR/test"
mkdir "$TEST_DIR/data"
"$TEST_DIR/test" "$TEST_DIR/data" "$ROOT/camera/assets/frames"
python3 - "$TEST_DIR/data" <<'PY'
from pathlib import Path
from PIL import Image
import sys
photos = list((Path(sys.argv[1]) / 'camera/photos').iterdir())
assert len(photos) == 6 and all(p.suffix == '.jpg' for p in photos)
for photo in photos:
    with Image.open(photo) as im:
        im.load()
        assert im.size == (32, 24) and im.format == 'JPEG'
print('Six filtered JPEGs decoded; no incomplete files after forced write failure')
sizes = []
for photo in (Path(sys.argv[1]) / 'rotation-test/camera/photos').glob('*.jpg'):
    with Image.open(photo) as im:
        im.load()
        sizes.append(im.size)
assert sorted(sizes) == [(480, 640), (640, 480), (640, 480)]
print('Portrait output is 480x640; legacy and imported originals remain 640x480')
for a, (cw, ch) in enumerate([(972, 729), (768, 1024), (972, 972), (960, 540)]):
    for p in range(4):
        edge = 0 if p == 0 else min(cw, ch) // 16
        with Image.open(Path(sys.argv[1]) / f'formats-test/camera/photos/format-{a}-{p}.jpg') as im:
            im.load()
            assert im.size == (cw + edge * 2, ch + edge * (2 if p == 3 else 4))
            # Crop center stays the source center in every format, without tint
            # leaking from paper. JPEG chroma rounding allows a small tolerance.
            r, g, b = im.getpixel((edge + cw // 2, edge + ch // 2))
            assert abs(r-127) < 4 and abs(g-127) < 4 and abs(b-64) < 4
            if p in (1, 2):
                actual = im.getpixel((im.width // 2, im.height - edge))
                assert min(actual) > 170  # Light generated paper, not a dark missing texture.
                assert actual[0] >= actual[2] - 3
print('All 16 imagegen-frame JPEGs decoded; geometry, crop and light paper verified')
PY
