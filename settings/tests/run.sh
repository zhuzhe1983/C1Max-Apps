#!/bin/sh
# Run inside the Linux builder with .deps/lvgl available.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p .build/settings-tests
c++ -std=c++17 -g -fsanitize=address,undefined -ffunction-sections -fdata-sections \
    -Wl,--gc-sections,--wrap=execvp -DLV_CONF_PATH="\"$PWD/shared/lv_conf.h\"" \
    -I.build/include -Ishared -I.deps/lvgl -Ishared/tinyalsa/include \
    settings/tests/wifi_transaction_test.cpp -o .build/settings-tests/wifi-test
python3 settings/tests/wifi_scenarios.py .build/settings-tests/wifi-test
