#include "apu.hpp"
#include "audio.hpp"
#include "InfoNES.h"
#include "InfoNES_System.h"
#include "InfoNES_pAPU.h"
#include "Nes_Apu.h"
#include "Blip_Buffer.h"
#include <algorithm>
#include <cstdio>

uint32_t c1_nes_apu_cycles=0;
namespace {
Nes_Apu apu;
Blip_Buffer buffer;
bool ready=false;
int last_access=0;
constexpr int rate=44100,ntsc_clock=1789773;

// K6502 exposes instruction-level timestamps, not individual bus cycles.
// read_status evaluates t-1 and t; keep that first phase after the preceding
// access even at the beginning of a frame or within a read/modify/write.
int access_time(bool read=false){
    last_access=std::max(int(c1_nes_apu_cycles),last_access+(read?1:0));
    return last_access;
}

int dmc_read(void*,nes_addr_t address){
    BYTE *banks[]={ROMBANK0,ROMBANK1,ROMBANK2,ROMBANK3};
    auto *bank=banks[(address>>13)&3];
    return bank?bank[address&0x1fff]:0;
}
void write_register(WORD address,BYTE value){
    if(ready)apu.write_register(access_time(),address,value);
}
}
ApuWritefunc pAPUSoundRegs[20]={
    write_register,write_register,write_register,write_register,
    write_register,write_register,write_register,write_register,
    write_register,write_register,write_register,write_register,
    write_register,write_register,write_register,write_register,
    write_register,write_register,write_register,write_register
};
void ApuWriteControl(WORD address,BYTE value){write_register(address,value);}
uint8_t c1_nes_apu_status(){return ready?uint8_t(apu.read_status(access_time(true))):0;}
void c1_nes_apu_frame_counter(uint8_t value){write_register(0x4017,value);}
bool c1_nes_apu_irq_pending(){return ready&&apu.earliest_irq(c1_nes_apu_cycles)<=int(c1_nes_apu_cycles);}

void InfoNES_pAPUInit(){
    ready=false;c1_nes_apu_cycles=0;last_access=0;
    InfoNES_SoundClose();buffer.clear();
    auto error=buffer.set_sample_rate(rate,50);
    if(error){std::fprintf(stderr,"NES: APU buffer: %s\n",error);return;}
    buffer.clock_rate(ntsc_clock);buffer.bass_freq(90);
    apu.output(&buffer);apu.volume(0.85);apu.dmc_reader(dmc_read);
    apu.reset(false);ready=true;
    InfoNES_SoundInit();InfoNES_SoundOpen(735,rate);
    std::fprintf(stderr,"NES: Nes_Snd_Emu NTSC, band-limited 44100 Hz\n");
}
void InfoNES_pAPUDone(){ready=false;InfoNES_SoundClose();buffer.clear();}
void InfoNES_pAPUVsync(){
    if(ready){
        int elapsed=std::max(int(c1_nes_apu_cycles),last_access);
        apu.end_frame(elapsed);buffer.end_frame(elapsed);
        int16_t mono[1024];long count;
        while((count=buffer.read_samples(mono,1024))>0)nes_audio::output(int(count),mono);
    }
    c1_nes_apu_cycles=0;last_access=0;
}
