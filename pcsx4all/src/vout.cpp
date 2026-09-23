// GPL-2.0-or-later. Native C1Max output for PCSX4all gpulib.
// Geometry follows upstream vout_port.cpp; no pixel/line dropping stage.
#include "port.h"
#include "gpu/gpulib/gpu.h"
void vout_update(){
    video_frame((const uint16_t*)gpu.vram,gpu.screen.x,gpu.screen.y,
        gpu.screen.hres,gpu.screen.vres,gpu.screen.h,gpu.status.rgb24);
}
int vout_init(){return 0;}
int vout_finish(){return 0;}
void vout_blank(){video_blank();}
void vout_set_config(const gpulib_config_t*){}
