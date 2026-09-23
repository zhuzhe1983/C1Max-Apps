#!/bin/sh
set -eu
APP=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
mkdir -p "$APP/../.build/qa/dosbox"
c++ -std=c++17 -fsanitize=address,undefined -g -I"$APP/../.deps/dosbox-pure/libretro-common/include" "$APP/tests/input_test.cpp" -o "$APP/../.build/qa/dosbox/input-test"
"$APP/../.build/qa/dosbox/input-test"
