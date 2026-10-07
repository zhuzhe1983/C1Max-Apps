#!/bin/sh
# Run inside the Linux builder (shared/net.cpp uses Linux process supervision).
set -eu
cd "$(dirname "$0")/../.."
mkdir -p .build/crosspoint-tests
c++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -Ishared -I.build/include -Icrosspoint/src crosspoint/tests/pdf_test.cpp crosspoint/src/pdf.cpp shared/net.cpp -o .build/crosspoint-tests/pdf-test-linux
python3 - <<'PY'
import tempfile,subprocess
with tempfile.TemporaryDirectory() as d:subprocess.run(['.build/crosspoint-tests/pdf-test-linux',d],check=True,timeout=20)
PY
