#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$(mktemp /tmp/c1max-hotkey-unit.XXXXXX)"
trap 'rm -f "$BIN"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer "$ROOT/tests/hotkey_test.c" -o "$BIN"
"$BIN"
