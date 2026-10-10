#pragma once
#include <algorithm>
#include <cstdint>

namespace nes_audio {
// Signed mono samples arrive DC-filtered and band-limited from Nes_Snd_Emu.
// Apply only system volume here, without altering oscillator balance or pitch.
class Mixer {
    struct Gain { int current=0,target=0,left=0; } gain_[2];
    int ramp_=220;
public:
    void reset(int rate) { *this=Mixer{};ramp_=std::max(1,rate/200); }
    void volume(long left,long right,int minimum,int maximum) {
        const long values[]={left,right};
        for(int c=0;c<2;c++) {
            int target=maximum>minimum?int((int64_t(std::clamp(values[c],long(minimum),long(maximum)))-minimum)*65536/(int64_t(maximum)-minimum)):0;
            if(gain_[c].target!=target){gain_[c].target=target;gain_[c].left=ramp_;}
        }
    }
    void sample(int16_t mono,int16_t *stereo) {
        for(int c=0;c<2;c++) {
            auto &g=gain_[c];
            if(g.left){g.current+=(g.target-g.current)/g.left;--g.left;}
            stereo[c]=int16_t(int64_t(mono)*g.current/65536);
        }
    }
};

// Called every VBlank, even with a PCM handle: after resume a broken driver
// may accept writes without providing back-pressure. Never use it as a clock.
class FrameClock {
    uint64_t next_=0;
public:
    void reset(){next_=0;}
    unsigned delay(uint64_t now,unsigned interval=16667) {
        if(!next_||now>next_+100000)next_=now;
        next_+=interval;
        return next_>now?unsigned(next_-now):0;
    }
};

bool open(int samples_per_sync,int rate);
void close();
bool active();
// BOOTTIME microseconds, so actual device suspend is included in the gap.
void service(uint64_t now);
void output(int samples,const int16_t *mono);
}
