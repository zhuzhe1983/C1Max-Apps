#pragma once
#include <cstdint>

class FrameWatchdog {
    uint32_t last_=0;
public:
    void reset(uint32_t now){last_=now;}
    void frame(uint32_t now){last_=now;}
    void resume(uint32_t now){last_=now;}
    bool stalled(uint32_t now,bool paused)const{
        uint32_t elapsed=now-last_;
        // A stale snapshot earlier than the newest frame is not a huge stall.
        // Half-range ordering also allows the normal 49-day tick wraparound.
        return !paused&&elapsed>15000&&elapsed<0x80000000u;
    }
};
