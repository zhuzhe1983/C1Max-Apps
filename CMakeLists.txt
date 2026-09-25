cmake_minimum_required(VERSION 3.16)
project(c1max_apps LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shared/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(.deps/lvgl EXCLUDE_FROM_ALL)
add_library(tinyalsa STATIC shared/tinyalsa/src/pcm.c shared/tinyalsa/src/pcm_hw.c shared/tinyalsa/src/mixer.c shared/tinyalsa/src/mixer_hw.c shared/tinyalsa/src/limits.c)
target_include_directories(tinyalsa PUBLIC shared/tinyalsa/include)
add_executable(c1max-launcher launcher/src/launcher.c launcher/src/typeface.c)
target_include_directories(c1max-launcher PRIVATE .deps/lvgl/src/libs/tiny_ttf)
target_link_libraries(c1max-launcher m)
add_executable(c1max-hotkey launcher/src/hotkey.c)
add_executable(c1max-touch launcher/src/inject.c)
add_executable(c1max-piano piano/src/piano.c)
target_link_libraries(c1max-piano tinyalsa m)
add_executable(c1max-nes nes/src/InfoNES_System_C1Max.cpp .deps/InfoNES/src/InfoNES.cpp .deps/InfoNES/src/InfoNES_Mapper.cpp .deps/InfoNES/src/InfoNES_pAPU.cpp .deps/InfoNES/src/K6502.cpp launcher/src/typeface.c)
target_include_directories(c1max-nes PRIVATE .deps/InfoNES/src .deps/lvgl/src/libs/tiny_ttf)
target_link_libraries(c1max-nes tinyalsa m)
add_library(c1net STATIC shared/net.cpp streamplayer/src/client.cpp)
target_include_directories(c1net PUBLIC shared streamplayer/src .build/include)
target_link_libraries(c1net pthread)
add_executable(c1max-streamplayer streamplayer/src/main.cpp streamplayer/src/segment_stream.cpp streamplayer/src/subtitle_stream.cpp streamplayer/src/subtitles.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-streamplayer c1net lvgl tinyalsa m)
# The stock dynamic MPlayer uses O32/FP64/NaN2008 while our static apps use
# the general cross-toolchain ABI. Build its tiny decode adapter separately.
file(GLOB_RECURSE YUV_PIPE_HEADERS CONFIGURE_DEPENDS streamplayer/vendor/ffmpeg42/*.h)
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/c1max-yuv-pipe.so"
    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/streamplayer/tools/build-yuv-pipe.sh" "${CMAKE_BINARY_DIR}/c1max-yuv-pipe.so"
    DEPENDS streamplayer/src/yuv_pipe.c streamplayer/tools/build-yuv-pipe.sh ${YUV_PIPE_HEADERS}
    VERBATIM)
add_custom_target(c1max-yuv-pipe ALL DEPENDS "${CMAKE_BINARY_DIR}/c1max-yuv-pipe.so")
add_executable(c1max-api-test tests/api_test.cpp)
target_link_libraries(c1max-api-test c1net)

target_compile_options(c1max-api-test PRIVATE -UNDEBUG)

add_executable(c1max-activate tools/activate.c)
add_executable(c1max-capture tools/capture.c)
add_executable(c1max-calculator calculator/src/main.cpp calculator/src/engine.cpp calculator/src/model.cpp shared/display.cpp shared/keyboard.cpp)
target_include_directories(c1max-calculator PRIVATE shared)
target_link_libraries(c1max-calculator lvgl m)
add_executable(c1max-calendar calendar/src/main.cpp calendar/src/calendar_model.cpp calendar/src/calendar_store.cpp shared/display.cpp shared/keyboard.cpp)
target_include_directories(c1max-calendar PRIVATE shared)
target_link_libraries(c1max-calendar c1net lvgl m)

add_subdirectory(terminal)
set_target_properties(c1max-terminal PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(c1max-volume shared/volume.c)
target_link_libraries(c1max-volume tinyalsa)
add_executable(c1max-key-test tests/key_inject.c)
add_executable(c1max-gomoku gomoku/src/main.cpp gomoku/src/game.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-gomoku c1net lvgl m)
add_subdirectory(pcsx4all)
add_subdirectory(processing)

add_subdirectory(dosbox)

add_executable(c1max-airtune airtune/src/main.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-airtune c1net lvgl tinyalsa m)
target_include_directories(c1max-airtune PRIVATE .deps/lvgl/src/libs/tiny_ttf)

# libmobi's built-in miniz keeps Kindle/AZW3 extraction independent of target
# system libraries. DRM decryption and the XML writer are not needed here.
set(USE_ENCRYPTION OFF CACHE BOOL "" FORCE)
set(USE_LIBXML2 OFF CACHE BOOL "" FORCE)
set(USE_ZLIB OFF CACHE BOOL "" FORCE)
set(USE_MINIZ ON CACHE BOOL "" FORCE)
set(USE_XMLWRITER OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(.deps/libmobi-0.12 "${CMAKE_BINARY_DIR}/libmobi" EXCLUDE_FROM_ALL)
add_executable(c1max-crosspoint crosspoint/src/main.cpp crosspoint/src/reader.cpp crosspoint/src/text.cpp crosspoint/src/epub.cpp crosspoint/src/opds.cpp crosspoint/src/transfer.cpp crosspoint/vendor/tinyxml2/tinyxml2.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-crosspoint c1net lvgl mobi m)
target_include_directories(c1max-crosspoint PRIVATE .deps/lvgl/src/libs/tiny_ttf .deps/libmobi-0.12/src crosspoint/vendor/tinyxml2)

add_executable(c1max-camera camera/src/main.cpp camera/src/filter.cpp camera/src/photo.cpp camera/src/album.cpp camera/src/frame_art.cpp camera/src/shutter_sound.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-camera lvgl tinyalsa m)
target_include_directories(c1max-camera PRIVATE .deps/lvgl/src/libs/tiny_ttf .deps/lvgl/src/libs/gltf/stb_image camera/src)

# The compact mail client uses TLS 1.2 with X.509 host verification; credentials
# never go over an unencrypted POP3 or SMTP session.
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MBEDTLS_LIBRARY ON CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(.deps/mbedtls-2.28.10 "${CMAKE_BINARY_DIR}/mbedtls" EXCLUDE_FROM_ALL)
add_executable(c1max-mail mail/src/main.cpp mail/src/protocol.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-mail c1net lvgl mbedtls mbedx509 mbedcrypto m)
target_include_directories(c1max-mail PRIVATE .deps/lvgl/src/libs/tiny_ttf)

# A compact Xpdf text extractor provides actual PDF text support without
# carrying a Qt viewer or rasterizer into the 128 MB target.
set(NO_FONTCONFIG ON CACHE BOOL "" FORCE)
add_subdirectory(.deps/xpdf-4.06 "${CMAKE_BINARY_DIR}/xpdf" EXCLUDE_FROM_ALL)
set_target_properties(pdftotext PROPERTIES OUTPUT_NAME c1max-pdftotext RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(c1max-crosspoint pdftotext)

# Optional applications stay local; a clean checkout builds only public apps.
option(C1_BUILD_LOCAL_APPS "Build applications from ignored local configuration" OFF)
if(C1_BUILD_LOCAL_APPS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/config/apps.local.cmake" OPTIONAL)
endif()
