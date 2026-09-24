// GPL-2.0-or-later. Compact physical-key legends fit the 98px margins at 16:9.
#pragma once
#include <cstring>
#include "../../shared/side_guide.hpp"
namespace dosguide {
constexpr int Width=98,Height=340;
inline void panel(std::vector<uint32_t>&out,bool font,int mode){
    out.assign(Width*Height,0xff0b121a);
    auto line=[&](int y,const char*cn,const char*en,int size=13,uint32_t color=0xff9caeba){
        if(font){int w=typeface_width(cn,size);typeface_draw(out.data(),Width,Height,(Width-w)/2,y,cn,color,size);return;}
        int x=(Width-int(strlen(en))*8)/2;
        for(const unsigned char *p=(const unsigned char*)en;*p;p++,x+=8)if(*p<128)
            for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(x+i>=0&&x+i<Width&&y+j<Height&&(font8x8_basic[*p][j]&(1<<i)))out[(y+j)*Width+x+i]=color;
    };
    auto key=[&](int x,int y,const char *s){
        for(int yy=y;yy<y+23;yy++)for(int xx=x;xx<x+23;xx++)out[yy*Width+xx]=(xx==x||xx==x+22||yy==y||yy==y+22)?0xff344451:0xff192733;
        if(font)typeface_draw(out.data(),Width,Height,x+(23-typeface_width(s,16))/2,y,s,0xffa7dcd8,16);
        else for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(font8x8_basic[(unsigned char)*s][j]&(1<<i))out[(y+j+7)*Width+x+i+7]=0xffa7dcd8;
    };
    if(mode==0){
        line(20,"游戏键盘","GAME KEYS",16,0xffd6e3eb);
        key(38,64,"W");key(11,98,"A");key(38,98,"S");key(65,98,"D");line(137,"方向键","ARROWS",14);
        line(179,"J   Ctrl","J  Ctrl",16,0xffd6e3eb);line(212,"K   Alt","K  Alt",16,0xffd6e3eb);
        line(245,"U   空格","U  Space",16,0xffd6e3eb);line(278,"I   回车","I  Enter",16,0xffd6e3eb);line(315,"Shift 可组合","Hold Shift",12);
    }else if(mode==1||mode==8){
        line(20,"文本键盘","TEXT KEYS",16,0xffd6e3eb);line(65,"字母直接输入","Type keys",14);
        line(105,"Shift+Q…P","Shift+Q..P");line(128,"数字 1…0","1..0");
        line(170,"Shift+B  :","Shift+B :");line(201,"Shift+C  /","Shift+C /");line(232,"Shift+Z  .","Shift+Z .");
        line(276,"双击 Shift","2x Shift",14,0xffa7dcd8);line(310,mode==8?"当前：大写":"当前：小写",mode==8?"CAPS ON":"lowercase",13);
    }else if(mode==2){
        line(20,"常用按键","CONTROLS",16,0xffd6e3eb);
        line(65,"回车 确认","Enter: OK",14,0xffd6e3eb);line(94,"返回 Esc","Back: Esc",14);line(123,"退格 删除","BS: delete",14);
        line(161,"触摸 左键","Touch: LMB",14);
        line(200,"短按拍照","Tap Camera",14,0xffa7dcd8);line(224,"切换前缀","Key prefix");
        line(260,"长按拍照","Hold Camera",14,0xffd6e3eb);line(284,"键盘/画幅菜单","Keys / view",12);
        line(315,"电源长按5秒退出","Hold Power 5s",12);
    }else{
        line(20,"一次性前缀","NEXT KEY",16,0xffd6e3eb);
        if(mode==3){line(65,"方向 / 导航","NAV",16,0xffa7dcd8);line(110,"WASD 方向","WASD move");line(145,"Q/E 首/尾","Q/E Home/End",12);line(180,"Z/X 翻页","Z/X PgUp/Dn",12);line(215,"B  Delete","B Delete");line(250,"空格 Tab","Space Tab");}
        if(mode==4){line(65,"F 功能键","F KEYS",16,0xffa7dcd8);line(115,"Q W E R","Q W E R");line(139,"F1 F2 F3 F4","F1 F2 F3 F4",12);line(180,"T Y U I","T Y U I");line(204,"F5 F6 F7 F8","F5 F6 F7 F8",12);line(245,"O P A S","O P A S");line(269,"F9…F12","F9..F12");}
        if(mode==5||mode==6){line(80,mode==5?"Ctrl":"Alt",mode==5?"Ctrl":"Alt",20,0xffa7dcd8);line(143,"再按字母键","Then letter");line(184,"组合键可长按","Hold combo",12);}
        if(mode==7){line(65,"额外符号","SYMBOLS",16,0xffa7dcd8);line(103,"Q[  W]","Q[ W]");line(129,"E{  R}","E{ R}");line(155,"T<  Y>","T< Y>");line(181,"U=  I+","U= I+");line(207,"O_  P\\","O_ P\\");line(233,"A\"  S'","A\" S'");line(259,"D` F! G| H^","D`F!G|H^",11);}
        line(311,"返回 取消前缀","Back: cancel",11);
    }
}
}
