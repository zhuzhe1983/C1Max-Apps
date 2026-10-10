#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$(mktemp /tmp/c1max-nes-audio.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
# ESTRPIPE is Linux's suspended-stream error; macOS has no such errno name.
FLAGS=()
if [ "$(uname -s)" = Darwin ]; then FLAGS+=(-DESTRPIPE=86); fi
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    "${FLAGS[@]}" -I"$ROOT/nes/src" -I"$ROOT/shared/tinyalsa/include" \
    "$ROOT/nes/src/audio.cpp" "$ROOT/nes/tests/audio_test.cpp" -o "$BIN"
"$BIN"
