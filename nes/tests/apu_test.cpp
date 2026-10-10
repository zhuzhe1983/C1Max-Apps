#include "apu.hpp"
#include "InfoNES.h"
#include "InfoNES_pAPU.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

BYTE memory[8192];
BYTE *ROMBANK0=memory,*ROMBANK1=memory,*ROMBANK2=memory,*ROMBANK3=memory;
static std::vector<int16_t> pcm;
void InfoNES_SoundInit(){}
void InfoNES_SoundClose(){}
int InfoNES_SoundOpen(int count,int rate){assert(count==735&&rate==44100);return 1;}
namespace nes_audio {void output(int count,const int16_t *p){pcm.insert(pcm.end(),p,p+count);}}
static void reset(){InfoNES_pAPUInit();pcm.clear();}
static void write(int address,int value){pAPUSoundRegs[address-0x4000](address,value);}
static void frames(int count){for(int i=0;i<count;i++){c1_nes_apu_cycles+=29829;InfoNES_pAPUVsync();}}
static double energy(size_t first,size_t last){
    double sum=0;for(size_t i=first;i<std::min(last,pcm.size());i++)sum+=double(pcm[i])*pcm[i];return sum;
}
int main(){
    reset();frames(60);assert(pcm.size()>44090&&pcm.size()<44110);
    assert(energy(1000,pcm.size())==0);
    reset();ApuWriteControl(0x4015,1);
    write(0x4000,0xbf);write(0x4001,0);write(0x4002,253);write(0x4003,0x08);
    assert((c1_nes_apu_status()&1)!=0);frames(60);
    int crossings=0;for(size_t i=1001;i<pcm.size();i++)if(pcm[i-1]<=0&&pcm[i]>0)crossings++;
    // NTSC CPU/(16*(253+1)) = 440.396 Hz. Old renderer ignored +1 and sweep clocks.
    assert(crossings>=427&&crossings<=433);assert(energy(1000,pcm.size())>1e9);
    ApuWriteControl(0x4015,0);assert(!(c1_nes_apu_status()&1));
    auto old=pcm.size();frames(20);assert(energy(old+5000,pcm.size())==0);

    // Noise envelope must decay; the previous unsigned phase could never be <0.
    reset();ApuWriteControl(0x4015,8);write(0x400c,0);write(0x400e,4);write(0x400f,0xf8);frames(12);
    assert(energy(0,2000)>1e6);assert(energy(7000,pcm.size())==0);
    reset();ApuWriteControl(0x4015,8);write(0x400c,0x3f);write(0x400e,4);write(0x400f,0xf8);frames(12);
    assert(energy(7000,pcm.size())>1e6);auto long_noise=pcm;
    reset();ApuWriteControl(0x4015,8);write(0x400c,0x3f);write(0x400e,0x84);write(0x400f,0xf8);frames(12);
    assert(pcm!=long_noise&&energy(7000,pcm.size())>1e6);

    // APU status and $4017 use the same clock as oscillator writes, including
    // the frame IRQ flag/acknowledgement; no APU_Reg[0x4015] out-of-bounds access.
    reset();c1_nes_apu_cycles=29840;assert(c1_nes_apu_irq_pending());
    assert(c1_nes_apu_status()&0x40);assert(!c1_nes_apu_irq_pending());
    c1_nes_apu_frame_counter(0xc0);InfoNES_pAPUVsync();
    c1_nes_apu_cycles=31000;assert(!c1_nes_apu_irq_pending());InfoNES_pAPUVsync();
    reset();std::fill_n(memory,8192,0xaa);write(0x4010,0x0f);write(0x4011,64);write(0x4012,0);write(0x4013,1);
    ApuWriteControl(0x4015,16);assert(c1_nes_apu_status()&16);frames(4);assert(!(c1_nes_apu_status()&16));
    assert(energy(0,pcm.size())>0);InfoNES_pAPUDone();
    std::puts("PASS NES APU: NTSC sample count, pulse pitch/status, noise envelope/modes, frame IRQ and DMC bank reads");
}
