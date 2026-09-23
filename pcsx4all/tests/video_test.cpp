#include "../src/video.hpp"
#include <cassert>
#include <vector>
#include <cstdio>
int main(){
    std::vector<uint16_t> vram(1024*512);
    psxvideo::Source s{vram.data(),0,0,640,480,480,false};
    psxvideo::Map m;assert(s.valid());m.set(s,true);
    // A one-pixel vertical stroke in an odd column survives native full-width
    // output. The former 640->320 decimator discarded this column entirely.
    for(int y=0;y<480;y++)vram[y*1024+101]=31;
    bool found=false;for(int x=0;x<800;x++)if(psxvideo::sample(s,m.row[10],m.x[x])==0xffff0000)found=true;
    assert(found);
    // No initial 480->240 decimation: native output uses odd scanlines too.
    bool odd=false;for(int j=0;j<340;j++)if((m.row[j]/1024)&1)odd=true;assert(odd);
    assert(psxvideo::bgr15(31)==0xffff0000);assert(psxvideo::bgr15(31<<5)==0xff00ff00);
    assert(psxvideo::bgr15(31<<10)==0xff0000ff);assert(psxvideo::bgr15(32767)==0xffffffff);
    for(int w:{256,320,368,384,512,640})for(int h:{240,256,480,512})for(bool wide:{false,true}){
        s.width=w;s.height=h;s.active_height=h;s.x=1023;s.y=511;m.set(s,wide);
        assert(m.left+(m.width)==(wide?800:626));
        for(int j=0;j<340;j++)for(int x=0;x<m.width;x++)(void)psxvideo::sample(s,m.row[j],m.x[x]);
    }
    s={vram.data(),0,0,320,240,224,false};m.set(s,false);
    assert(m.row[0]==-1&&m.row[339]==-1&&m.row[170]>=0);
    assert(psxvideo::sample(s,-1,0)==0xff000000);
    // Byte-wise wrapping also handles 24-bit FMV at the last VRAM byte.
    auto *b=(uint8_t*)vram.data();b[0xffffe]=0x12;b[0xfffff]=0x34;b[0]=0x56;
    s.rgb24=true;assert(psxvideo::sample(s,1024*512-1,0)==0xff123456);
    s.width=641;assert(!s.valid());s.width=320;s.active_height=241;assert(!s.valid());
    puts("Native video: high-resolution strokes, scanlines, colors, modes, borders and VRAM wrap passed");
}
