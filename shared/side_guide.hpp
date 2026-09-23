// GPL-2.0-or-later. Shared drawing primitives for physical-key side legends.
#pragma once
#include <algorithm>
#include <vector>
#include "../launcher/src/font8x8.h"
extern "C" {
#include "../launcher/src/typeface.h"
}
namespace sideguide {
constexpr int Width=173,Height=340;
inline void text(std::vector<uint32_t>&out,bool font,int x,int y,const char*cn,const char*en,int size,uint32_t color){
    if(font){typeface_draw(out.data(),Width,Height,x,y,cn,color,size);return;}
    int scale=size>=22?2:1;
    for(const unsigned char *c=(const unsigned char*)en;*c;c++,x+=8*scale){if(*c>=128)continue;
        for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(font8x8_basic[*c][j]&(1<<i))for(int dy=0;dy<scale;dy++)for(int dx=0;dx<scale;dx++){
            int px=x+i*scale+dx,py=y+j*scale+dy;if(px>=0&&px<Width&&py>=0&&py<Height)out[py*Width+px]=color;
        }
    }
}
inline void key(std::vector<uint32_t>&out,bool font,int x,int y,const char*letter,uint32_t color=0xffa7dcd8){
    for(int yy=y;yy<y+29;yy++)for(int xx=x;xx<x+29;xx++)out[yy*Width+xx]=(xx==x||xx==x+28||yy==y||yy==y+28)?0xff344451:0xff192733;
    int w=font?typeface_width(letter,20):8;text(out,font,x+(29-w)/2,y+1,letter,letter,20,color);
}
}
