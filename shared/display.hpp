#pragma once
#include "lvgl.h"
#include <cstdint>
namespace screen {
bool open();
void close();
extern bool quit;
extern bool playing;
extern bool tap;
uint32_t tick();
constexpr uint32_t KEY_EXIT=0x10000;
constexpr uint32_t KEY_SYMBOL=0x10001;
constexpr uint32_t KEY_HOME=0x10002;
constexpr uint32_t KEY_MODE=0x10003;
bool caps_lock();
uint32_t take_key();
// Single framebuffer owner: submit complete low-resolution RGB frames, then
// composite video and UI to an inactive page and present it in one operation.
bool video_begin();
void video_frame(const uint32_t *rgb,int width,int height,int aspect_n=1,int aspect_d=1);
void video_fit(bool width_fill);
void video_controls(bool visible);
void video_refresh(bool paused=false);
void video_end();
}
