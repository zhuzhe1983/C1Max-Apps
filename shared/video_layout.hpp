#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>

// Lookup tables keep divisions out of the 272,000-pixel composition loop.
struct VideoCaption {const uint32_t*pixels=nullptr;int width=0,height=0;};
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
    void compose(uint8_t *out,int stride,const uint32_t *rgb,int width,const uint32_t *ui,bool controls,bool full=false,const VideoCaption*caption=nullptr)const{
        // Native panel is 340 x 800, rotated CCW from the logical screen.
        bool caption_visible=caption&&caption->pixels&&caption->width>0&&caption->width<=752&&caption->height>0&&caption->height<=112&&!(controls&&full);
        int cx=caption_visible?(800-caption->width)/2:0,cy=caption_visible?(controls?206:326)-caption->height:0;
        for(int row=0;row<800;row++){
            auto *dst=reinterpret_cast<uint32_t*>(out+size_t(row)*stride);int sx=xs[799-row];
            for(int y=0;y<340;y++){
                if(controls&&(full||y<36||y>=218))dst[y]=ui[size_t(row)*340+y];
                else {
                    uint32_t color=rgb&&sx>=0&&ys[y]>=0?rgb[size_t(ys[y])*width+sx]:0xff000000;
                    int x=799-row-cx;
                    if(caption_visible&&x>=0&&x<caption->width&&y>=cy&&y<cy+caption->height){
                        uint32_t sub=caption->pixels[size_t(y-cy)*caption->width+x],a=sub>>24;
                        uint32_t rb=(((sub&0xff00ff)*a+(color&0xff00ff)*(255-a)+0x800080)>>8)&0xff00ff;
                        uint32_t g=(((sub&0x00ff00)*a+(color&0x00ff00)*(255-a)+0x008000)>>8)&0x00ff00;
                        // Exact endpoints preserve black strokes and opaque text.
                        if(a==255)color=0xff000000|(sub&0xffffff);else if(a)color=0xff000000|rb|g;
                    }
                    dst[y]=color;
                }
            }
        }
    }
};
