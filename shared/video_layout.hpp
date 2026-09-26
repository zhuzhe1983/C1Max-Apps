#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include "orientation.hpp"

// Lookup tables keep divisions out of the 272,000-pixel composition loop.
struct VideoCaption {const uint32_t*pixels=nullptr;int width=0,height=0;};
struct VideoLayout {
    int xs[800],ys[800];
    ScreenOrientation orientation;
    int controls_top=36,controls_bottom=218;
    void configure(int w,int h,int aspect_n,int aspect_d,bool width_fill,bool portrait=false){
        orientation.portrait=portrait;
        int sw=orientation.width(),sh=orientation.height();
        double ratio=double(aspect_n)/aspect_d;
        double scale=double(sw)/(w*ratio);
        if(!width_fill)scale=std::min(scale,double(sh)/h);
        double dw=w*ratio*scale,dh=h*scale,left=(sw-dw)/2,top=(sh-dh)/2;
        for(int x=0;x<sw;x++){double p=(x+0.5-left)*w/dw;xs[x]=p>=0&&p<w?int(p):-1;}
        for(int y=0;y<sh;y++){double p=(y+0.5-top)*h/dh;ys[y]=p>=0&&p<h?int(p):-1;}
    }
    void compose(uint8_t *out,int stride,const uint32_t *rgb,int width,const uint32_t *ui,bool controls,bool full=false,const VideoCaption*caption=nullptr)const{
        // Native panel is 340 x 800, rotated CCW from the logical screen.
        bool caption_visible=caption&&caption->pixels&&caption->width>0&&caption->width<=752&&caption->height>0&&caption->height<=112&&!(controls&&full);
        int cx=caption_visible?(orientation.width()-caption->width)/2:0,cy=caption_visible?(controls?controls_bottom-12:orientation.height()-14)-caption->height:0;
        if(orientation.portrait){
            // Portrait LVGL and video share the native 340x800 coordinates.
            // In the landscape view, the bottom control panel is on the left.
            for(int row=0;row<800;row++){
                auto*dst=reinterpret_cast<uint32_t*>(out+size_t(row)*stride);
                for(int x=0;x<340;x++){
                    if(controls&&(full||row<controls_top||row>=controls_bottom))dst[x]=ui[size_t(row)*340+x];
                    else {
                        uint32_t color=rgb&&xs[x]>=0&&ys[row]>=0?rgb[size_t(ys[row])*width+xs[x]]:0xff000000;
                        if(caption_visible&&x>=cx&&x<cx+caption->width&&row>=cy&&row<cy+caption->height){
                            uint32_t sub=caption->pixels[size_t(row-cy)*caption->width+x-cx],a=sub>>24;
                            uint32_t rb=(((sub&0xff00ff)*a+(color&0xff00ff)*(255-a)+0x800080)>>8)&0xff00ff;
                            uint32_t g=(((sub&0x00ff00)*a+(color&0x00ff00)*(255-a)+0x008000)>>8)&0x00ff00;
                            if(a==255)color=0xff000000|(sub&0xffffff);else if(a)color=0xff000000|rb|g;
                        }
                        dst[x]=color;
                    }
                }
            }
            return;
        }
        for(int row=0;row<800;row++){
            auto *dst=reinterpret_cast<uint32_t*>(out+size_t(row)*stride);int sx=xs[799-row];
            for(int y=0;y<340;y++){
                if(controls&&(full||y<controls_top||y>=controls_bottom))dst[y]=ui[size_t(row)*340+y];
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
