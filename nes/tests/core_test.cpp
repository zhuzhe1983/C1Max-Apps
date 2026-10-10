// Exercise the exact patched CPU and framebuffer conversion without opening
// display/input devices. Run with the MIPS ABI (InfoNES DWORD is unsigned long).
#define main nes_device_main
#include "../src/InfoNES_System_C1Max.cpp"
#undef main
#include "K6502.h"
#include "K6502_rw.h"
#include "apu.hpp"
#include <cassert>

int main(){
    static_assert(sizeof(DWORD)==4,"Run using the MIPS toolchain/QEMU");
    InfoNES_Init();
    assert(PPU_ScanTable[0]==SCAN_ON_SCREEN);
    assert(PPU_ScanTable[SCAN_VBLANK_START]==SCAN_VBLANK);
    fb_stride=1360;frame_sz=1360*800;fbmem=(uint8_t*)calloc(3,frame_sz);
    for(int x=0;x<VP_W;x++)lutx[x]=x*256/VP_W;
    for(int y=0;y<VP_H;y++)luty[y]=y*240/VP_H;
    const WORD colors[]={0,0x7fff,0xffff,0x7c00,0x3e0,0x1f,0x8000};
    const uint32_t expected[]={0xff000000,0xffffffff,0xffffffff,0xffff0000,0xff00ff00,0xff0000ff,0xff000000};
    for(unsigned i=0;i<sizeof(colors)/sizeof(colors[0]);i++){
        std::fill_n(WorkFrame,256*240,colors[i]);InfoNES_LoadFrame();
        for(int y=0;y<VP_H;y++)for(int x=0;x<VP_W;x++)assert(*(uint32_t*)(fbmem+(799-(VP_X0+x))*fb_stride+y*4)==expected[i]);
    }
    build_panels();virtual_controls=false;draw_controls();
    assert(region_to_pad(720,150)==0);
    for(bool touch:{true,false}){
        virtual_controls=touch;draw_controls();
        assert(region_to_pad(720,150)==(touch?NA:0));
        for(int y=0;y<VP_H;y++)for(int x=0;x<VP_W;x++)
            assert(*(uint32_t*)(fbmem+(799-(VP_X0+x))*fb_stride+y*4)==0xff000000);
    }
    // Optional synthetic render for margin typography inspection, not a
    // screenshot of a running game and never included in public screenshots.
    if(const char *path=getenv("C1_NES_TEST_FRAME")){
        FILE *f=fopen(path,"wb");assert(f);assert(fwrite(fbmem,1,frame_sz,f)==size_t(frame_sz));fclose(f);
    }
    free(fbmem);fbmem=nullptr;
    BYTE bank[8192]={0};ROMBANK0=ROMBANK1=ROMBANK2=ROMBANK3=bank;
    K6502_Init();K6502_Reset();
    // LDA #0; STA $4017; LDA $4015; JMP $0000. No cartridge is needed.
    const BYTE program[]={0xa9,0x00,0x8d,0x17,0x40,0xad,0x15,0x40,0x4c,0,0};
    std::copy(program,program+sizeof program,RAM);c1_nes_apu_cycles=0;
    K6502_Step(113);assert(c1_nes_apu_cycles>=113&&c1_nes_apu_cycles<120);
    assert(g_wPassedClocks<7);auto first=c1_nes_apu_cycles;
    K6502_Step(113);assert(c1_nes_apu_cycles>=226&&c1_nes_apu_cycles<233&&c1_nes_apu_cycles>first);
    assert(K6502_Read(0x4015)==0);
    puts("PASS NES core: CPU cycle accumulation across scanlines, APU I/O hooks, VBlank and RGB555 frames");
}
