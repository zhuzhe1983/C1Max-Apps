#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p .build/crosspoint-tests
c++ -std=c++17 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -Icrosspoint/src -Icrosspoint/vendor/tinyxml2 crosspoint/tests/opds_test.cpp \
  crosspoint/src/opds.cpp crosspoint/vendor/tinyxml2/tinyxml2.cpp -o .build/crosspoint-tests/opds-test
.build/crosspoint-tests/opds-test "$@"
