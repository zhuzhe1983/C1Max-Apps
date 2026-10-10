#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$(mktemp /tmp/c1max-nes-apu.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
GME="$ROOT/.deps/game-music-emu/gme"
"${CXX:-c++}" -std=c++17 -O1 -g -fsanitize=address,undefined \
    -I"$ROOT/nes/src" -I"$ROOT/.deps/InfoNES/src" -I"$GME" \
    "$ROOT/nes/src/apu.cpp" "$ROOT/nes/tests/apu_test.cpp" \
    "$GME/Nes_Apu.cpp" "$GME/Nes_Oscs.cpp" "$GME/Blip_Buffer.cpp" -o "$BIN"
ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$BIN"
