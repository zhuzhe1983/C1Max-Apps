#!/bin/sh
# Linux container only; no hardware, user data or network service is used.
set -eu
cd "$(dirname "$0")/../.."
exec python3 - <<'PY'
import os, pathlib, subprocess, tempfile
root = pathlib.Path.cwd()
with tempfile.TemporaryDirectory(prefix='c1-voice-ui-') as tmp:
    source = pathlib.Path(tmp)
    (source/'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.16)
project(voice_ui_tests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
add_compile_options(-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -ffunction-sections -fdata-sections)
add_link_options(-fsanitize=address,undefined -Wl,--gc-sections)
set(LV_BUILD_CONF_PATH "{root}/shared/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("{root}/.deps/lvgl" lvgl EXCLUDE_FROM_ALL)
# The pinned LVGL kerning cache places its entry after a 12-byte key. On
# 64-bit hosts that violates pointer alignment; MIPS32 only requires 4 bytes.
# Keep ASan and all other UBSan checks, excluding only that vendor source's
# host-only alignment check. Application code remains fully instrumented.
set_source_files_properties("{root}/.deps/lvgl/src/misc/cache/lv_cache_entry.c"
 TARGET_DIRECTORY lvgl PROPERTIES COMPILE_OPTIONS "-fno-sanitize=alignment")

file(GLOB vterm "{root}/terminal/vendor/libvterm/src/*.c")
file(GLOB alsa "{root}/shared/tinyalsa/src/*.c")
add_executable(terminal-ui "{root}/terminal/tests/voice_ui_test.cpp" "{root}/terminal/src/terminal.cpp" "{root}/terminal/src/pty.cpp" "{root}/terminal/tests/c1ime_stub.cpp" ${{vterm}})
add_executable(settings-ui "{root}/settings/tests/voice_ui_test.cpp" ${{alsa}})
foreach(name terminal-ui settings-ui)
 target_sources(${{name}} PRIVATE "{root}/terminal/src/voice.cpp" "{root}/shared/net.cpp" "{root}/terminal/tests/headless_display.cpp")
 target_include_directories(${{name}} PRIVATE "{root}/shared" "{root}/shared/tinyalsa/include" "{root}/terminal/vendor/libvterm/include" "{root}/.build/include")
 target_link_libraries(${{name}} lvgl m pthread dl)
endforeach()
''')
    subprocess.run(['cmake', '-S', str(source), '-B', str(source/'build')], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(source/'build'), '-j4'], check=True, stdout=subprocess.DEVNULL)
    # LVGL's persistent allocator is not torn down by the display fixture.
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0')
    subprocess.run([str(source/'build/settings-ui'), str(source/'data')], env=env, check=True, timeout=20)
    subprocess.run([str(source/'build/terminal-ui'), str(root)], env=env, check=True, timeout=20)
PY
