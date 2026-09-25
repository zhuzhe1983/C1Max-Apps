#!/usr/bin/env bash
set -euo pipefail
APPS="$(cd "$(dirname "$0")/.." && pwd)"
LOCAL_APPS=OFF
case "${1:-}" in
  --local) LOCAL_APPS=ON ;;
  '') ;;
  *) echo "Usage: $0 [--local]" >&2; exit 2 ;;
esac
if [ "$#" -gt 1 ]; then echo "Usage: $0 [--local]" >&2; exit 2; fi
if [ "$LOCAL_APPS" = ON ]; then
  python3 "$APPS/tools/fetch_deps.py" --local
else
  python3 "$APPS/tools/fetch_deps.py"
fi
docker build -q -t c1max-apps-builder:bookworm "$APPS/tools"
BUILDER=c1max-apps-builder:bookworm
if [ "$LOCAL_APPS" = ON ] && [ -f "$APPS/config/Dockerfile.local" ]; then
  BUILDER=c1max-apps-builder:local
  docker build -q -t "$BUILDER" -f "$APPS/config/Dockerfile.local" "$APPS/tools"
fi
"$APPS/linux-tools/build.sh"
mkdir -p "$APPS/.build"
# Resolve Git's file list on the host: submodule metadata is outside /work.
git -C "$APPS" ls-files --cached --others --exclude-standard -z > "$APPS/.build/package-sources"
docker run --rm -e C1_LOCAL_APPS="$LOCAL_APPS" -v "$APPS:/work" "$BUILDER" sh -ec '
  mkdir -p .build/include
  cp -R /usr/include/nlohmann .build/include/
  cp /etc/ssl/certs/ca-certificates.crt .build/ca-certificates.crt
  cmake -S . -B .build/mips -DCMAKE_TOOLCHAIN_FILE=tools/mipsel.cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DC1_BUILD_LOCAL_APPS="$C1_LOCAL_APPS"
  cmake --build .build/mips -j4
  if [ "$C1_LOCAL_APPS" = ON ]; then
    python3 tools/package.py --source-list .build/package-sources --local
  else
    python3 tools/package.py --source-list .build/package-sources
  fi
'
