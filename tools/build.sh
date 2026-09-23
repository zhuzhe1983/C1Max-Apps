#!/usr/bin/env bash
set -euo pipefail
APPS="$(cd "$(dirname "$0")/.." && pwd)"
python3 "$APPS/tools/fetch_deps.py"
docker build -q -t c1max-apps-builder:bookworm "$APPS/tools"
"$APPS/linux-tools/build.sh"
docker run --rm -v "$APPS:/work" c1max-apps-builder:bookworm sh -ec '
  mkdir -p .build/include
  cp -R /usr/include/nlohmann .build/include/
  cp /etc/ssl/certs/ca-certificates.crt .build/ca-certificates.crt
  cmake -S . -B .build/mips -DCMAKE_TOOLCHAIN_FILE=tools/mipsel.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
  cmake --build .build/mips -j4
  python3 tools/package.py
'
