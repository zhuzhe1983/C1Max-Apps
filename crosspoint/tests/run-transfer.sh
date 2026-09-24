#!/usr/bin/env bash
# Run inside Linux with GNU wget, Python 3 and a native C++17 compiler.
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p .build/crosspoint-tests
"${CXX:-g++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  -pthread -Ishared -I.build/include -Icrosspoint/src -Icrosspoint/vendor/tinyxml2 \
  crosspoint/tests/transfer_probe.cpp crosspoint/src/transfer.cpp crosspoint/src/opds.cpp \
  crosspoint/vendor/tinyxml2/tinyxml2.cpp shared/net.cpp -o .build/crosspoint-tests/transfer-probe
C1_APPS_ROOT="$PWD/.build/device" python3 crosspoint/tests/transfer_test.py .build/crosspoint-tests/transfer-probe
