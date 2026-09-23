#!/usr/bin/env bash
set -euo pipefail
TOOLS_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE="${C1_TOOLS_IMAGE:-c1max-linux-tools-builder:bookworm}"
mkdir -p "$TOOLS_DIR/.build"
python3 "$TOOLS_DIR/sources.py"
docker build -q -t "$IMAGE" "$TOOLS_DIR"
docker run --rm -v "$TOOLS_DIR:/work" -e "JOBS=${JOBS:-4}" "$IMAGE" bash /work/cross-build.sh
printf 'Runtime files: %s/.build/linux-tools\n' "$TOOLS_DIR"
