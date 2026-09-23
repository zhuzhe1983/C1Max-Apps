// Test-only display backend: runs the actual app, real PTY and LVGL software
// renderer into a PPM file without framebuffer, keyboard, SDL or a browser.
#include "display.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>
#include <set>

namespace screen {
bool quit = false, playing = false, tap = false;
static std::array<uint32_t, 800 * 340> pixels{};
static std::array<uint32_t, 800 * 24> scratch{};
static uint32_t started;
static bool refresh_test;
static std::set<uint64_t> refresh_frames;
static unsigned refresh_samples;
uint32_t tick() {
    timespec t{}; clock_gettime(CLOCK_MONOTONIC, &t);
    return uint32_t(uint64_t(t.tv_sec) * 1000 + t.tv_nsec / 1000000);
}
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *bytes) {
    auto *source = reinterpret_cast<uint32_t *>(bytes);
    for (int y = area->y1; y <= area->y2; ++y)
        for (int x = area->x1; x <= area->x2; ++x) pixels[size_t(y) * 800 + x] = *source++;
    lv_display_flush_ready(display);
}
bool open() {
    lv_init(); lv_tick_set_cb(tick);
    auto *display = lv_display_create(800, 340);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
    lv_display_set_buffers(display, scratch.data(), nullptr, sizeof(scratch), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush); started = tick();
    refresh_test = std::getenv("C1_TERMINAL_TEST_REFRESH") != nullptr;
    return true;
}
void close() {
    if (refresh_test) {
        if (refresh_samples != 4 || refresh_frames.size() != 4) {
            std::fprintf(stderr, "FAIL automatic refresh: %u samples, %zu distinct frames\n",
                         refresh_samples, refresh_frames.size());
            std::exit(2);
        }
        std::puts("PASS automatic refresh: four distinct screens without further key input");
    }
    const char *path = std::getenv("C1_TERMINAL_TEST_PPM");
    if (!path) return;
    if (FILE *file = std::fopen(path, "wb")) {
        std::fputs("P6\n800 340\n255\n", file);
        for (uint32_t pixel : pixels) {
            unsigned char rgb[]{uint8_t(pixel >> 16), uint8_t(pixel >> 8), uint8_t(pixel)};
            std::fwrite(rgb, sizeof(rgb), 1, file);
        }
        std::fclose(file);
    }
}
bool caps_lock() { return false; }
uint32_t take_key() {
    // Octal keeps shell input ASCII while producing real UTF-8 command output.
    static const std::string sample_command =
        "printf '\\033[2J\\033[H\\033[1;36mC1Max Terminal\\033[0m - real PTY / ANSI / UTF-8\\n"
        "0123456789  ABCDEFGHIJKLMNOPQRSTUVWXYZ  abcdefghijklmnopqrstuvwxyz\\n"
        "\\344\\270\\255\\346\\226\\207 \\346\\230\\276\\347\\244\\272  |  width: 2 cells\\n"
        "\\033[31mred \\033[32mgreen \\033[34mblue \\033[1mbold \\033[4munderline\\033[0m\\n"
        "\\033[48;5;24m256 color background\\033[0m  \\033[38;2;230;160;40mtruecolor\\033[0m\\n'\r";
    static const std::string refresh_command =
        "for n in 1 2 3 4; do printf '\\033[2J\\033[HREFRESH %s\\n' \"$n\"; sleep 1; done\r";
    const auto &command = refresh_test ? refresh_command : sample_command;
    static size_t offset = 0;
    uint32_t elapsed = tick() - started;
    if (refresh_test && refresh_samples < 4 && elapsed >= 750 + refresh_samples * 1000) {
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < 800 * 308; ++i) { hash ^= pixels[i]; hash *= 1099511628211ULL; }
        refresh_frames.insert(hash); ++refresh_samples;
    }
    if (elapsed > (refresh_test ? 4200u : 1500u)) { quit = true; return 0; }
    if (elapsed > 100 && offset < command.size()) return uint8_t(command[offset++]);
    return 0;
}
}
