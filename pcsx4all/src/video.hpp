// GPL-2.0-or-later. Sample native PS1 VRAM once, without a 320x240 intermediary.
#pragma once
#include <algorithm>
#include <cstdint>
namespace psxvideo {
struct Source {
    const uint16_t *vram=nullptr;
    int x=0,y=0,width=0,height=0,active_height=0;
    bool rgb24=false;
    bool valid()const{return vram&&width>0&&width<=640&&height>0&&height<=512&&active_height>0&&active_height<=height;}
};
struct Map {
    int x[800]{},row[340]{};
    int width=453,left=173;
    void set(const Source &s,bool wide){
        width=wide?800:453;left=wide?0:173;
        for(int i=0;i<width;i++)x[i]=(i*s.width/width)*(s.rgb24?3:1);
        const int top=(s.height-s.active_height)/2;
        for(int j=0;j<340;j++){
            int y=j*s.height/340-top;
            row[j]=(y<0||y>=s.active_height)?-1:((s.y+y)*1024+s.x)&(1024*512-1);
        }
    }
};
inline uint32_t bgr15(uint16_t p){
    unsigned r=p&31,g=(p>>5)&31,b=(p>>10)&31;
    return 0xff000000|((r<<3|r>>2)<<16)|((g<<3|g>>2)<<8)|(b<<3|b>>2);
}
inline uint32_t sample(const Source &s,int row,int x){
    if(row<0)return 0xff000000;
    if(!s.rgb24)return bgr15(s.vram[(row+x)&(1024*512-1)]);
    const auto *b=reinterpret_cast<const uint8_t*>(s.vram);unsigned at=unsigned(row)*2+x;
    return 0xff000000|(unsigned(b[at&0xfffff])<<16)|(unsigned(b[(at+1)&0xfffff])<<8)|b[(at+2)&0xfffff];
}
}
