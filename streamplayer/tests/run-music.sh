#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p .build/audio-tests
c++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Ishared -Istreamplayer/src -I.build/include streamplayer/tests/music_test.cpp streamplayer/src/client.cpp shared/net.cpp -pthread -o .build/audio-tests/music-test
python3 streamplayer/tests/music_test.py .build/audio-tests/music-test
