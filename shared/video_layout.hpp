#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

// Lookup tables keep divisions out of the 272,000-pixel composition loop.
struct VideoLayout {
    int xs[800],ys[340];
    void configure(int w,int h,int aspect_n,int aspect_d,bool width_fill){
        double ratio=double(aspect_n)/aspect_d;
        double scale=800.0/(w*ratio);
        if(!width_fill)scale=std::min(scale,340.0/h);
        double dw=w*ratio*scale,dh=h*scale,left=(800-dw)/2,top=(340-dh)/2;
        for(int x=0;x<800;x++){double p=(x+0.5-left)*w/dw;xs[x]=p>=0&&p<w?int(p):-1;}
        for(int y=0;y<340;y++){double p=(y+0.5-top)*h/dh;ys[y]=p>=0&&p<h?int(p):-1;}
    }
    void compose(uint8_t *out,int stride,const uint32_t *rgb,int width,const uint32_t *ui,bool controls)const{
        // Native panel is 340 x 800, rotated CCW from the logical screen.
        for(int row=0;row<800;row++){
            auto *dst=reinterpret_cast<uint32_t*>(out+size_t(row)*stride);int sx=xs[799-row];
            for(int y=0;y<340;y++){
                if(controls&&(y<36||y>=218))dst[y]=ui[size_t(row)*340+y];
                else dst[y]=rgb&&sx>=0&&ys[y]>=0?rgb[size_t(ys[y])*width+sx]:0xff000000;
            }
        }
    }
};
