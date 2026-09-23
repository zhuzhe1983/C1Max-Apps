// GPL-2.0-or-later. Physical PS1 controls in the side margins.
#pragma once
#include "../../shared/side_guide.hpp"
namespace psxguide {
using namespace sideguide;
inline void panel(std::vector<uint32_t>&out,bool font,bool right,bool interpreter){
    out.assign(Width*Height,0xff0b121a);
    if(!right){
        text(out,font,16,24,"方向","DIRECTION",20,0xffd6e3eb);
        text(out,font,16,52,"拍照  切换画幅","Camera: width",14,0xff9caeba);
        key(out,font,73,80,"W");key(out,font,36,117,"A");key(out,font,73,117,"S");key(out,font,110,117,"D");
        text(out,font,37,159,"移动 / 选择","Move / select",16,0xff829aaa);
        text(out,font,16,204,"肩键","SHOULDERS",18,0xffd6e3eb);
        key(out,font,12,237,"Q");text(out,font,47,241,"L1","L1",17,0xff9caeba);
        key(out,font,95,237,"E");text(out,font,130,241,"R1","R1",17,0xff9caeba);
        key(out,font,12,277,"Z");text(out,font,47,281,"L2","L2",17,0xff9caeba);
        key(out,font,95,277,"C");text(out,font,130,281,"R2","R2",17,0xff9caeba);
        text(out,font,16,317,interpreter?"兼容模式":"快速模式",interpreter?"Interpreter":"Dynamic CPU",12,0xff697f90);
    }else{
        text(out,font,16,24,"动作键","ACTIONS",20,0xffd6e3eb);
        const char *keys[]={"J","K","U","I"};const char *shapes[]={"×","○","□","△"};const char *ascii[]={"X","O","[]","/\\"};
        const uint32_t colors[]={0xff91b8ed,0xffe99b99,0xffd7a1dc,0xff93d7b5};
        for(int i=0;i<4;i++){int y=72+i*37;key(out,font,18,y,keys[i],colors[i]);text(out,font,68,y-3,shapes[i],ascii[i],27,colors[i]);}
        text(out,font,16,229,"回车  Start","Enter  Start",16,0xffd6e3eb);
        text(out,font,16,254,"空格  Select","Space  Select",16,0xff9caeba);
        text(out,font,16,284,"返回  暂停/存档","Back   Menu/save",16,0xffd6e3eb);
        text(out,font,16,310,"电源  回菜单","Power  Launcher",16,0xff9caeba);
    }
}
}
