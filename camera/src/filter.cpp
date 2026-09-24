#include "filter.hpp"
#include <algorithm>
#include <cstdlib>

namespace camera {
const char *filter_name(Filter f){static const char *names[]={"COLOR","MONO","SEPIA","WARM","COOL","FADED"};auto i=unsigned(f);return i<6?names[i]:"COLOR";}
uint32_t apply_filter(uint32_t p,Filter f,int x,int y,int w,int h){int r=(p>>16)&255,g=(p>>8)&255,b=p&255;switch(f){
    case Filter::Original:break;
    case Filter::Mono:{int l=(77*r+150*g+29*b)>>8;r=g=b=l;break;}
    case Filter::Sepia:{int rr=(101*r+197*g+48*b)>>8,gg=(89*r+176*g+43*b)>>8,bb=(70*r+137*g+34*b)>>8;r=rr;g=gg;b=bb;break;}
    case Filter::Warm:r=std::min(255,(r*11+50)/10);g=std::min(255,(g*103+50)/100);b=std::max(0,(b*9+50)/10);break;
    case Filter::Cool:r=std::max(0,(r*93+50)/100);g=std::min(255,(g*101+50)/100);b=std::min(255,(b*11+50)/10);break;
    case Filter::Faded:{int l=(77*r+150*g+29*b)>>8;r=(r*4+l*2+3)/6+18;g=(g*4+l*2+3)/6+12;b=(b*4+l*2+3)/6+4;if(w>0&&h>0){int dx=std::abs(x-w/2)*256/std::max(1,w/2),dy=std::abs(y-h/2)*256/std::max(1,h/2);int edge=std::min(256,(dx*dx+dy*dy)>>8),scale=256-(edge*15)/256;r=(r*scale)>>8;g=(g*scale)>>8;b=(b*scale)>>8;}break;}
    case Filter::Count:break;
    }r=std::clamp(r,0,255);g=std::clamp(g,0,255);b=std::clamp(b,0,255);return 0xff000000u|(uint32_t(r)<<16)|(uint32_t(g)<<8)|uint32_t(b);}
}
