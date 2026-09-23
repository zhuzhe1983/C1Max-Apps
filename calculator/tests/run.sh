#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_bin=$(mktemp "${TMPDIR:-/tmp}/c1max-calculator-test.XXXXXX")
trap 'rm -f "$test_bin"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I "$app_dir/src" "$app_dir/tests/engine_test.cpp" \
  "$app_dir/src/engine.cpp" "$app_dir/src/model.cpp" -o "$test_bin"
"$test_bin"
