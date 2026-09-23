#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$(mktemp /tmp/c1max-keymap.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined "$ROOT/tests/keymap_test.cpp" -o "$BIN"
"$BIN"
