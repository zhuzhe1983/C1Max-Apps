#!/bin/sh
set -eu
# A standalone artifact path is required; all build files stay temporary.
exec python3 - "$0" "${1:?usage: render.sh /absolute/output.ppm [--refresh]}" "${2:-}" <<'PY'
import os, pathlib, subprocess, sys, tempfile
root = pathlib.Path(sys.argv[1]).resolve().parent.parent
apps = root.parent
output = pathlib.Path(sys.argv[2]).resolve()
with tempfile.TemporaryDirectory(prefix='c1max-terminal-render-') as tmp:
    directory = pathlib.Path(tmp)
    source = directory/'source'
    source.mkdir()
    (source/'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.16)
project(terminal_render LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LV_BUILD_CONF_PATH "{apps}/shared/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("{apps}/.deps/lvgl" lvgl EXCLUDE_FROM_ALL)
file(GLOB vterm_sources "{root}/vendor/libvterm/src/*.c")
add_executable(terminal-render "{root}/src/main.cpp" "{root}/src/terminal.cpp"
    "{root}/src/pty.cpp" "{root}/tests/headless_display.cpp" ${{vterm_sources}})
target_include_directories(terminal-render PRIVATE "{apps}/shared" "{root}/vendor/libvterm/include")
target_link_libraries(terminal-render lvgl m)
''')
    subprocess.run(['cmake', '-S', str(source), '-B', str(directory/'build'), '-DCMAKE_BUILD_TYPE=Release'], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['cmake', '--build', str(directory/'build'), '-j4'], check=True, stdout=subprocess.DEVNULL)
    staged = directory/'apps'
    staged.mkdir()
    (staged/'terminal').symlink_to(root, target_is_directory=True)
    (staged/'shared').mkdir()
    (staged/'shared/NotoSansSC-Regular.ttf').symlink_to(apps/'shared/fonts/NotoSansSC-Regular.ttf')
    env = dict(os.environ, C1_APPS_ROOT=str(staged), C1_APPS_DATA=str(directory/'data'),
               C1_TERMINAL_SHELL='/bin/sh', C1_TERMINAL_TEST_PPM=str(output))
    if sys.argv[3] == '--refresh':
        env['C1_TERMINAL_TEST_REFRESH'] = '1'
    subprocess.run([str(directory/'build/terminal-render')], env=env, check=True, timeout=15)
    print('Rendered actual terminal to', output)
PY
