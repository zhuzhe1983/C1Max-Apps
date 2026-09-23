# Included from apps/CMakeLists.txt with add_subdirectory(terminal).
set(VTERM_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/libvterm")
add_library(c1vterm STATIC
    ${VTERM_SOURCE_DIR}/src/encoding.c ${VTERM_SOURCE_DIR}/src/keyboard.c
    ${VTERM_SOURCE_DIR}/src/mouse.c ${VTERM_SOURCE_DIR}/src/parser.c
    ${VTERM_SOURCE_DIR}/src/pen.c ${VTERM_SOURCE_DIR}/src/screen.c
    ${VTERM_SOURCE_DIR}/src/state.c ${VTERM_SOURCE_DIR}/src/unicode.c
    ${VTERM_SOURCE_DIR}/src/vterm.c)
target_include_directories(c1vterm PUBLIC ${VTERM_SOURCE_DIR}/include)
target_include_directories(c1vterm PRIVATE ${VTERM_SOURCE_DIR}/src)
add_executable(c1max-terminal src/main.cpp src/terminal.cpp src/pty.cpp
    ../shared/display.cpp ../shared/keyboard.cpp)
target_include_directories(c1max-terminal PRIVATE ../shared)
target_link_libraries(c1max-terminal c1vterm lvgl m)
