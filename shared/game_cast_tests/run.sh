#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$(mktemp /tmp/c1max-game-cast.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    -isystem "$ROOT/.build/include" -pthread "$ROOT/shared/game_cast.cpp" \
    "$ROOT/shared/game_cast_tests/client_test.cpp" -o "$BIN"
"$BIN"
