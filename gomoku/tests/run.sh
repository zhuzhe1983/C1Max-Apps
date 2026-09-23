#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=$(mktemp "${TMPDIR:-/tmp}/c1max-gomoku-test.XXXXXX")
trap 'rm -f "$BIN"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -UNDEBUG -fsanitize=address,undefined -I"$HERE/src" "$HERE/src/game.cpp" "$HERE/tests/game_test.cpp" -o "$BIN"
"$BIN"
