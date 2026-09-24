#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
mkdir -p .build/crosspoint-tests
flags=(-O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all)
"${CC:-cc}" "${flags[@]}" -DMINIZ_NO_STDIO -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_WRITING_APIS -c .deps/libmobi-0.12/src/miniz.c -o .build/crosspoint-tests/miniz.o
"${CXX:-c++}" -std=c++17 "${flags[@]}" -Icrosspoint/src -Icrosspoint/vendor/tinyxml2 -I.deps/libmobi-0.12/src \
 crosspoint/tests/epub_probe.cpp crosspoint/src/epub.cpp crosspoint/src/text.cpp \
 crosspoint/vendor/tinyxml2/tinyxml2.cpp .build/crosspoint-tests/miniz.o -o .build/crosspoint-tests/epub-probe
python3 crosspoint/tests/epub_test.py .build/crosspoint-tests/epub-probe
