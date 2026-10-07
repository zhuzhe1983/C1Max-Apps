// Input tests use the real LVGL renderer without a framebuffer or media device.
#include "../terminal/tests/headless_display.cpp"
namespace screen {
void portrait(bool){} bool video_begin(){return false;}
void video_frame(const uint32_t*,int,int,int,int){} void video_fit(bool){}
void video_controls(bool,bool){} void video_controls_area(int,int){}
void video_caption(const uint32_t*,int,int){} void video_refresh(bool){} void video_end(){}
}
