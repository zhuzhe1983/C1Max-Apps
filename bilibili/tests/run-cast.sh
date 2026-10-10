#!/usr/bin/env bash
# Host entry point. Fixture DNS/TLS and fake MPlayer exist only in this container.
set -euo pipefail
APPS="$(cd "$(dirname "$0")/../.." && pwd)"
docker run --rm --add-host fixture.bilivideo.com:127.0.0.1 -v "$APPS:/work" c1max-apps-builder:bookworm sh -ec '
  cmake -S bilibili/tests -B .build/bilibili-cast-tests/cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address,undefined" -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
  cmake --build .build/bilibili-cast-tests/cmake -j4
  python3 bilibili/tests/relay_test.py .build/bilibili-cast-tests/cmake/relay-probe
  install -m755 bilibili/tests/fake_mplayer.py /usr/bin/mplayer
  mkdir -p /tmp/bili-cast-player
  C1_APPS_DATA=/tmp/bili-cast-player .build/bilibili-cast-tests/cmake/player-cast-test
'
