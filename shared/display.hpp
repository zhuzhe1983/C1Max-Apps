#pragma once
#include "lvgl.h"
#include <cstdint>
namespace screen {
bool open();
void close();
// Playback may reflow LVGL to 340x800; the default remains 800x340.
void portrait(bool enabled);
extern bool quit;
extern bool playing;
extern bool tap;
uint32_t tick();
constexpr uint32_t KEY_EXIT=0x10000;
constexpr uint32_t KEY_SYMBOL=0x10001;
constexpr uint32_t KEY_HOME=0x10002;
constexpr uint32_t KEY_MODE=0x10003;
constexpr uint32_t KEY_HOME_LONG=0x10004;
constexpr uint32_t KEY_FONT_DOWN=0x10005,KEY_FONT_UP=0x10006;
bool caps_lock();
uint32_t take_key();
// Single framebuffer owner: submit complete low-resolution RGB frames, then
// composite video and UI to an inactive page and present it in one operation.
bool video_begin();
void video_frame(const uint32_t *rgb,int width,int height,int aspect_n=1,int aspect_d=1);
void video_fit(bool width_fill);
void video_controls(bool visible,bool full=false);
void video_controls_area(int top,int bottom);
// Straight-alpha ARGB caption, independent of source video crop/scale.
// Kept within the panel and raised above the controls when they are visible.
void video_caption(const uint32_t*argb,int width,int height);
void video_refresh(bool paused=false);
void video_end();
}
