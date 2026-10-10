cmake_minimum_required(VERSION 3.16)
project(c1max_apps LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)
# glibc < 2.34 keeps pthread in a separate libpthread.a, and a static link
# resolves the weak pthread references behind std::async/std::thread to null
# (a 2026-09-28 release built with Ubuntu GCC 10 crashed at startup in
# Bilibili). Link the whole archive there; merged glibc needs nothing.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <features.h>
#if !__GLIBC_PREREQ(2, 34)
#error libpthread is separate
#endif
int main() { return 0; }" C1_GLIBC_MERGED_PTHREAD)
if(NOT C1_GLIBC_MERGED_PTHREAD AND CMAKE_EXE_LINKER_FLAGS MATCHES "-static")
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -Wl,--whole-archive -lpthread -Wl,--no-whole-archive")
endif()
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/shared/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(.deps/lvgl EXCLUDE_FROM_ALL)
add_library(tinyalsa STATIC shared/tinyalsa/src/pcm.c shared/tinyalsa/src/pcm_hw.c shared/tinyalsa/src/mixer.c shared/tinyalsa/src/mixer_hw.c shared/tinyalsa/src/limits.c)
target_include_directories(tinyalsa PUBLIC shared/tinyalsa/include)
add_library(c1audio STATIC shared/audio_client.c)
target_include_directories(c1audio PUBLIC shared)
add_executable(c1max-audio shared/audio_service.cpp)
target_link_libraries(c1max-audio c1audio)
add_executable(c1max-launcher launcher/src/launcher.c launcher/src/typeface.c)
target_include_directories(c1max-launcher PRIVATE .deps/lvgl/src/libs/tiny_ttf)
target_link_libraries(c1max-launcher c1audio m)
add_executable(c1max-hotkey launcher/src/hotkey.c)
target_link_libraries(c1max-hotkey tinyalsa)
add_executable(c1max-touch launcher/src/inject.c)
add_executable(c1max-piano piano/src/piano.c)
target_link_libraries(c1max-piano c1audio tinyalsa m)
# Patch only a private build copy of InfoNES for the cycle-timed sound bridge.
set(C1_NES_CORE "${CMAKE_BINARY_DIR}/nes-core")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS nes/tools/prepare_core.py)
execute_process(COMMAND python3 "${CMAKE_SOURCE_DIR}/nes/tools/prepare_core.py"
    "${CMAKE_SOURCE_DIR}/.deps/InfoNES/src" "${C1_NES_CORE}" RESULT_VARIABLE C1_NES_PATCH_RESULT)
if(NOT C1_NES_PATCH_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not prepare InfoNES audio hooks")
endif()
add_library(c1nes-apu STATIC .deps/game-music-emu/gme/Nes_Apu.cpp .deps/game-music-emu/gme/Nes_Oscs.cpp .deps/game-music-emu/gme/Blip_Buffer.cpp)
target_include_directories(c1nes-apu PUBLIC .deps/game-music-emu/gme)
add_executable(c1max-nes nes/src/InfoNES_System_C1Max.cpp nes/src/audio.cpp nes/src/apu.cpp
    ${C1_NES_CORE}/InfoNES.cpp ${C1_NES_CORE}/InfoNES_Mapper.cpp ${C1_NES_CORE}/K6502.cpp launcher/src/typeface.c)
target_include_directories(c1max-nes PRIVATE nes/src ${C1_NES_CORE} .deps/lvgl/src/libs/tiny_ttf)
target_link_libraries(c1max-nes tinyalsa c1nes-apu m)
add_executable(c1max-nes-audio-test EXCLUDE_FROM_ALL nes/tests/audio_test.cpp nes/src/audio.cpp)
target_include_directories(c1max-nes-audio-test PRIVATE nes/src shared/tinyalsa/include)
target_compile_options(c1max-nes-audio-test PRIVATE -UNDEBUG)
add_executable(c1max-nes-apu-test EXCLUDE_FROM_ALL nes/tests/apu_test.cpp nes/src/apu.cpp)
target_include_directories(c1max-nes-apu-test PRIVATE nes/src ${C1_NES_CORE})
target_link_libraries(c1max-nes-apu-test c1nes-apu m)
target_compile_options(c1max-nes-apu-test PRIVATE -UNDEBUG)
add_executable(c1max-nes-core-test EXCLUDE_FROM_ALL nes/tests/core_test.cpp nes/src/audio.cpp nes/src/apu.cpp
    ${C1_NES_CORE}/InfoNES.cpp ${C1_NES_CORE}/InfoNES_Mapper.cpp ${C1_NES_CORE}/K6502.cpp launcher/src/typeface.c)
target_include_directories(c1max-nes-core-test PRIVATE nes/src ${C1_NES_CORE} .deps/lvgl/src/libs/tiny_ttf)
target_link_libraries(c1max-nes-core-test tinyalsa c1nes-apu m)
target_compile_options(c1max-nes-core-test PRIVATE -UNDEBUG)
add_library(c1net STATIC shared/net.cpp streamplayer/src/client.cpp)
target_include_directories(c1net PUBLIC shared streamplayer/src .build/include)
target_link_libraries(c1net pthread)
add_executable(c1max-streamplayer streamplayer/src/main.cpp streamplayer/src/segment_stream.cpp streamplayer/src/subtitle_stream.cpp streamplayer/src/subtitles.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-streamplayer c1audio c1net lvgl tinyalsa m)
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
add_executable(c1max-settings settings/src/main.cpp terminal/src/voice.cpp shared/display.cpp shared/keyboard.cpp)
target_include_directories(c1max-settings PRIVATE shared .deps/lvgl/src/libs/tiny_ttf)
target_link_libraries(c1max-settings c1net lvgl tinyalsa m)
add_executable(c1max-calendar calendar/src/main.cpp calendar/src/calendar_model.cpp calendar/src/calendar_store.cpp shared/display.cpp shared/keyboard.cpp)
target_include_directories(c1max-calendar PRIVATE shared)
target_link_libraries(c1max-calendar c1net lvgl m)

# C1Max Terminal uses librime's core as an in-process input layer. Build its
# four small static dependencies into a private prefix first so the target
# stays self-contained and does not depend on a device-side Rime installation.
set(_C1_RIME_SOURCE "${CMAKE_SOURCE_DIR}/.deps/librime")
set(_C1_RIME_STAGE "${CMAKE_BINARY_DIR}/_c1_rime_stage")
set(_C1_RIME_STAMP "${_C1_RIME_STAGE}/.installed")
if(NOT EXISTS "${_C1_RIME_STAMP}")
    file(MAKE_DIRECTORY "${_C1_RIME_STAGE}")
    set(_C1_RIME_COMMON -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX=${_C1_RIME_STAGE}
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON)
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND _C1_RIME_COMMON -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} -S "${_C1_RIME_SOURCE}/deps/yaml-cpp"
        -B "${CMAKE_BINARY_DIR}/_c1_rime_build/yaml-cpp" ${_C1_RIME_COMMON}
        -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_CPP_BUILD_CONTRIB=OFF
        -DYAML_CPP_INSTALL=ON RESULT_VARIABLE _c1_rime_rc)
    if(_c1_rime_rc EQUAL 0)
        execute_process(COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}/_c1_rime_build/yaml-cpp"
            --target install -j4 RESULT_VARIABLE _c1_rime_rc)
    endif()
    if(_c1_rime_rc EQUAL 0)
        execute_process(COMMAND ${CMAKE_COMMAND} -S "${_C1_RIME_SOURCE}/deps/leveldb"
            -B "${CMAKE_BINARY_DIR}/_c1_rime_build/leveldb" ${_C1_RIME_COMMON}
            -DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF RESULT_VARIABLE _c1_rime_rc)
        if(_c1_rime_rc EQUAL 0)
            execute_process(COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}/_c1_rime_build/leveldb"
                --target install -j4 RESULT_VARIABLE _c1_rime_rc)
        endif()
    endif()
    if(_c1_rime_rc EQUAL 0)
        execute_process(COMMAND ${CMAKE_COMMAND} -S "${_C1_RIME_SOURCE}/deps/marisa-trie"
            -B "${CMAKE_BINARY_DIR}/_c1_rime_build/marisa" ${_C1_RIME_COMMON} RESULT_VARIABLE _c1_rime_rc)
        if(_c1_rime_rc EQUAL 0)
            execute_process(COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}/_c1_rime_build/marisa"
                --target install -j4 RESULT_VARIABLE _c1_rime_rc)
        endif()
    endif()
    if(_c1_rime_rc EQUAL 0)
        execute_process(COMMAND ${CMAKE_COMMAND} -S "${_C1_RIME_SOURCE}/deps/opencc"
            -B "${CMAKE_BINARY_DIR}/_c1_rime_build/opencc" ${_C1_RIME_COMMON}
            -DUSE_SYSTEM_MARISA=OFF -DBUILD_TESTING=OFF -DOPENCC_BUILD_TOOLS=OFF RESULT_VARIABLE _c1_rime_rc)
        if(_c1_rime_rc EQUAL 0)
            # OpenCC's normal install target builds and runs its dictionary
            # converter, which would be a MIPS executable during a cross build.
            # Rime only needs the static library and headers; the runtime
            # dictionaries are already bundled in terminal/assets/rime-data.
            execute_process(COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}/_c1_rime_build/opencc"
                --target libopencc -j4 RESULT_VARIABLE _c1_rime_rc)
            if(_c1_rime_rc EQUAL 0)
                file(MAKE_DIRECTORY "${_C1_RIME_STAGE}/lib" "${_C1_RIME_STAGE}/include/opencc")
                execute_process(COMMAND ${CMAKE_COMMAND} -E copy
                    "${CMAKE_BINARY_DIR}/_c1_rime_build/opencc/src/libopencc.a"
                    "${_C1_RIME_STAGE}/lib/libopencc.a" RESULT_VARIABLE _c1_rime_rc)
                file(GLOB _C1_OPENCC_HEADERS
                    "${_C1_RIME_SOURCE}/deps/opencc/src/*.h"
                    "${_C1_RIME_SOURCE}/deps/opencc/src/*.hpp")
                file(COPY ${_C1_OPENCC_HEADERS} DESTINATION "${_C1_RIME_STAGE}/include/opencc")
                file(COPY "${CMAKE_BINARY_DIR}/_c1_rime_build/opencc/src/opencc_config.h"
                    "${CMAKE_BINARY_DIR}/_c1_rime_build/opencc/src/Opencc_Export.h"
                    DESTINATION "${_C1_RIME_STAGE}/include/opencc")
            endif()
        endif()
    endif()
    if(NOT _c1_rime_rc EQUAL 0)
        message(FATAL_ERROR "Failed to build a vendored librime dependency")
    endif()
    file(WRITE "${_C1_RIME_STAMP}" "done\n")
endif()
list(PREPEND CMAKE_PREFIX_PATH "${_C1_RIME_STAGE}")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_TEST OFF CACHE BOOL "" FORCE)
set(BUILD_DATA OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLE OFF CACHE BOOL "" FORCE)
set(ENABLE_LOGGING OFF CACHE BOOL "" FORCE)
set(ENABLE_THREADING ON CACHE BOOL "" FORCE)
set(ENABLE_TIMESTAMP OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory("${_C1_RIME_SOURCE}" "${CMAKE_BINARY_DIR}/librime" EXCLUDE_FROM_ALL)
add_library(c1ime STATIC shared/c1ime.cpp)
target_include_directories(c1ime PUBLIC shared "${_C1_RIME_SOURCE}/include" "${_C1_RIME_SOURCE}/src")
target_link_libraries(c1ime PUBLIC rime-static pthread)
add_executable(rime_deployer EXCLUDE_FROM_ALL tools/ime_deploy.cpp)
target_link_libraries(rime_deployer PRIVATE c1ime)
set_target_properties(rime_deployer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/librime/bin")
add_executable(c1ime-test EXCLUDE_FROM_ALL terminal/tests/ime_test.cpp)
target_compile_options(c1ime-test PRIVATE -UNDEBUG)
target_link_libraries(c1ime-test PRIVATE c1ime)

add_subdirectory(terminal)
set_target_properties(c1max-terminal PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

add_executable(c1max-volume shared/volume.c)
target_link_libraries(c1max-volume tinyalsa)
add_executable(c1max-power-guard shared/power_guard.c)
add_executable(c1max-key-test tests/key_inject.c)
add_executable(c1max-gomoku gomoku/src/main.cpp gomoku/src/game.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-gomoku c1net lvgl m)
add_subdirectory(pcsx4all)
add_subdirectory(processing)

add_subdirectory(dosbox)

add_executable(c1max-airtune airtune/src/main.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-airtune c1audio c1net lvgl tinyalsa m)
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
add_executable(c1max-crosspoint crosspoint/src/main.cpp crosspoint/src/reader.cpp crosspoint/src/pdf.cpp crosspoint/src/text.cpp crosspoint/src/epub.cpp crosspoint/src/opds.cpp crosspoint/src/transfer.cpp crosspoint/vendor/tinyxml2/tinyxml2.cpp shared/display.cpp shared/keyboard.cpp)
target_link_libraries(c1max-crosspoint c1net c1ime lvgl mobi m)
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

add_executable(c1max-bilibili bilibili/src/main.cpp bilibili/src/api.cpp bilibili/src/player.cpp shared/display.cpp shared/keyboard.cpp)
target_include_directories(c1max-bilibili PRIVATE .deps/lvgl/src/libs/tiny_ttf streamplayer/src)
target_link_libraries(c1max-bilibili c1net lvgl tinyalsa mbedcrypto m)

add_executable(c1max-bilibili-test EXCLUDE_FROM_ALL bilibili/tests/api_test.cpp bilibili/src/api.cpp)
target_include_directories(c1max-bilibili-test PRIVATE bilibili/src)
target_compile_options(c1max-bilibili-test PRIVATE -UNDEBUG)
target_link_libraries(c1max-bilibili-test c1net mbedcrypto)

# Run only on C1 Max with an explicit public BV id and temporary data directory.
add_executable(c1max-bilibili-player-test EXCLUDE_FROM_ALL bilibili/tests/player_device_test.cpp bilibili/src/api.cpp bilibili/src/player.cpp)
target_include_directories(c1max-bilibili-player-test PRIVATE bilibili/src streamplayer/src)
target_link_libraries(c1max-bilibili-player-test c1net lvgl mbedcrypto m)

# A compact Xpdf text extractor provides actual PDF text support without
# carrying a Qt viewer or rasterizer into the 128 MB target.
set(NO_FONTCONFIG ON CACHE BOOL "" FORCE)
add_subdirectory(.deps/xpdf-4.06 "${CMAKE_BINARY_DIR}/xpdf" EXCLUDE_FROM_ALL)
set_target_properties(pdftotext PROPERTIES OUTPUT_NAME c1max-pdftotext RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_dependencies(c1max-crosspoint pdftotext)

add_executable(c1max-hidpilot hidpilot/src/main.cpp hidpilot/src/usb.cpp shared/display.cpp shared/keyboard.cpp)
target_include_directories(c1max-hidpilot PRIVATE .deps/lvgl/src/libs/tiny_ttf .deps/lvgl/src/libs/gltf/stb_image)
target_link_libraries(c1max-hidpilot c1net lvgl m)
add_executable(c1max-hidpilot-usb hidpilot/src/gadget.cpp)
set(USE_MBEDTLS ON CACHE BOOL "" FORCE)
set(ENET_NO_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory(.deps/moonlight-embedded/third_party/moonlight-common-c "${CMAKE_BINARY_DIR}/moonlight-common" EXCLUDE_FROM_ALL)
add_library(c1moon STATIC moonpilot/src/gamestream.cpp moonpilot/src/remote.cpp crosspoint/vendor/tinyxml2/tinyxml2.cpp)
target_include_directories(c1moon PUBLIC moonpilot/src streamplayer/src PRIVATE crosspoint/vendor/tinyxml2)
target_link_libraries(c1moon PUBLIC c1net moonlight-common-c mbedtls mbedx509 mbedcrypto pthread)
add_executable(c1max-moonpilot moonpilot/src/main.cpp moonpilot/src/agent.cpp moonpilot/src/voice.cpp moonpilot/src/frame_image.cpp shared/display.cpp shared/keyboard.cpp)
target_include_directories(c1max-moonpilot PRIVATE .deps/lvgl/src/libs/tiny_ttf)
target_link_libraries(c1max-moonpilot c1moon lvgl m)
add_executable(c1max-moonpilot-service-test EXCLUDE_FROM_ALL moonpilot/tools/service-test.cpp moonpilot/src/agent.cpp moonpilot/src/voice.cpp)
target_link_libraries(c1max-moonpilot-service-test c1net)
add_executable(c1max-moonpilot-probe EXCLUDE_FROM_ALL moonpilot/tools/probe.cpp)
target_compile_options(c1max-moonpilot-probe PRIVATE -UNDEBUG)
target_link_libraries(c1max-moonpilot-probe c1moon)
add_executable(c1max-moonpilot-frame-test EXCLUDE_FROM_ALL moonpilot/tests/frame_test.cpp)
target_include_directories(c1max-moonpilot-frame-test PRIVATE streamplayer/src)
target_compile_options(c1max-moonpilot-frame-test PRIVATE -UNDEBUG)

add_subdirectory(tox)

# Optional applications stay local; a clean checkout builds only public apps.
option(C1_BUILD_LOCAL_APPS "Build applications from ignored local configuration" OFF)
if(C1_BUILD_LOCAL_APPS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/config/apps.local.cmake" OPTIONAL)
endif()

add_library(c1store STATIC appstore/src/store.cpp)
target_include_directories(c1store PUBLIC appstore/src)
target_link_libraries(c1store PUBLIC c1audio c1net mbedcrypto)
add_executable(c1max-appstore appstore/src/main.cpp shared/display.cpp shared/keyboard.cpp)
target_include_directories(c1max-appstore PRIVATE .deps/lvgl/src/libs/tiny_ttf)
target_link_libraries(c1max-appstore c1store lvgl m)

add_executable(c1max-crosspoint-ui-test EXCLUDE_FROM_ALL crosspoint/tests/ui_test.cpp crosspoint/src/reader.cpp crosspoint/src/pdf.cpp crosspoint/src/text.cpp crosspoint/src/epub.cpp crosspoint/src/opds.cpp crosspoint/src/transfer.cpp crosspoint/vendor/tinyxml2/tinyxml2.cpp terminal/tests/headless_display.cpp)
target_include_directories(c1max-crosspoint-ui-test PRIVATE .deps/lvgl/src/libs/tiny_ttf .deps/libmobi-0.12/src crosspoint/vendor/tinyxml2)
target_compile_options(c1max-crosspoint-ui-test PRIVATE -UNDEBUG)
target_link_libraries(c1max-crosspoint-ui-test c1net c1ime lvgl mobi m)
add_executable(c1max-crosspoint-pdf-probe EXCLUDE_FROM_ALL crosspoint/tests/pdf_probe.cpp crosspoint/src/pdf.cpp)
target_include_directories(c1max-crosspoint-pdf-probe PRIVATE crosspoint/src)
target_link_libraries(c1max-crosspoint-pdf-probe c1net)
add_executable(c1max-airtune-focus-test EXCLUDE_FROM_ALL airtune/tests/focus_test.cpp terminal/tests/headless_display.cpp)
target_include_directories(c1max-airtune-focus-test PRIVATE .deps/lvgl/src/libs/tiny_ttf)
target_compile_options(c1max-airtune-focus-test PRIVATE -UNDEBUG)
target_link_libraries(c1max-airtune-focus-test c1audio c1net tinyalsa lvgl m)

add_library(c1text STATIC shared/text_input.cpp)
target_link_libraries(c1text PUBLIC c1ime c1net lvgl)
foreach(app airtune streamplayer bilibili calendar mail settings moonpilot crosspoint tox)
    target_link_libraries(c1max-${app} c1text)
endforeach()
target_link_libraries(c1max-airtune-focus-test c1text)
target_link_libraries(c1max-crosspoint-ui-test c1text)
add_executable(c1max-text-input-test EXCLUDE_FROM_ALL tests/text_input_test.cpp terminal/tests/headless_display.cpp)
target_compile_options(c1max-text-input-test PRIVATE -UNDEBUG)
target_link_libraries(c1max-text-input-test c1text)
# Real app input handlers with headless LVGL; no sends, settings writes or media.
set(input_apps streamplayer bilibili calendar mail settings tox moonpilot)
set(input_number 0)
foreach(app IN LISTS input_apps)
    math(EXPR input_number "${input_number}+1")
    get_target_property(input_sources c1max-${app} SOURCES)
    list(FILTER input_sources EXCLUDE REGEX "(^|/)(main.cpp|display.cpp|keyboard.cpp)$")
    if(app STREQUAL "tox")
        # Sources of targets created in a subdirectory are relative to it.
        set(input_sources "")
    endif()
    add_executable(c1max-${app}-input-test EXCLUDE_FROM_ALL tests/text_apps_test.cpp tests/input_display.cpp ${input_sources})
    get_target_property(input_includes c1max-${app} INCLUDE_DIRECTORIES)
    if(input_includes)
        target_include_directories(c1max-${app}-input-test PRIVATE ${input_includes})
    endif()
    get_target_property(input_links c1max-${app} LINK_LIBRARIES)
    target_link_libraries(c1max-${app}-input-test ${input_links})
    target_compile_definitions(c1max-${app}-input-test PRIVATE INPUT_APP=${input_number})
    target_compile_options(c1max-${app}-input-test PRIVATE -UNDEBUG)
endforeach()
