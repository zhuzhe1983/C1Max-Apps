#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TEST_BIN=$(mktemp "${TMPDIR:-/tmp}/c1max-calendar-test.XXXXXX")
trap 'rm -f "$TEST_BIN"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -UNDEBUG \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I"$HERE/src" "$HERE/src/calendar_model.cpp" "$HERE/src/calendar_store.cpp" "$HERE/tests/model_test.cpp" -o "$TEST_BIN"
"$TEST_BIN"
