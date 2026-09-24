#!/usr/bin/env bash
# Linux: links the real network/client code (uses sys/prctl.h).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .build/defaults-tests
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -pthread \
 -Ishared -Istreamplayer/src -I.build/include tests/default_servers_test.cpp \
 shared/net.cpp streamplayer/src/client.cpp -o .build/defaults-tests/defaults-test
.build/defaults-tests/defaults-test
python3 tests/local_defaults_test.py
