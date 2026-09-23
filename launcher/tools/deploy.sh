#!/usr/bin/env bash
set -euo pipefail
APPS="$(cd "$(dirname "$0")/../.." && pwd)"
: "${ANDROID_SERIAL:?Set ANDROID_SERIAL to the target C1Max serial}"
exec python3 "$APPS/tools/deploy.py" --serial "$ANDROID_SERIAL" --start
