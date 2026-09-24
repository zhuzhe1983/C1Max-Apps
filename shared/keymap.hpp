#pragma once
#include <cstdint>
#include "power_hold.hpp"

// Kernel codes are kept here so the state machine can be tested without Linux
// headers, a framebuffer or LVGL. This is the C1 Max keycap layout, not a PC.
namespace keyboard {
constexpr uint32_t Back=0x10000, Symbol=0x10001, Home=0x10002, Mode=0x10003, HomeLong=0x10004;
constexpr uint32_t FontDown=0x10005,FontUp=0x10006;
class Keymap {
    bool held_[2]={false,false}, used_=false, caps_=false, tap_=false;
    uint64_t down_=0, released_=0;
    PowerHold power_;
public:
    bool caps_lock() const { return caps_; }
    void reset() { *this=Keymap{}; }
    void lost_events() { held_[0]=held_[1]=false; used_=true; tap_=false; power_.reset(); }
    uint32_t tick(uint64_t ms) { return power_.tick(ms)==PowerHold::Exit?HomeLong:0; }
    uint32_t event(unsigned code,int value,uint64_t ms) {
        if(code==116){auto action=power_.event(value,ms);return action==PowerHold::Hint?Home:action==PowerHold::Exit?HomeLong:0;}
        if(code==42||code==54) {
            unsigned i=code==54;
            if(value==1) { if(!held_[0]&&!held_[1]) { used_=false; down_=ms; } held_[i]=true; }
            else if(value==0&&held_[i]) {
                held_[i]=false;
                if(!held_[0]&&!held_[1]) {
                    bool short_tap=!used_&&ms>=down_&&ms-down_<=350;
                    if(short_tap&&tap_&&ms>=released_&&ms-released_<=350) {
                        caps_=!caps_; tap_=false; return Mode;
                    }
                    tap_=short_tap; released_=ms;
                }
            }
            return 0;
        }
        if(value!=1&&value!=2)return 0;
        used_=true; tap_=false;
        if((held_[0]||held_[1])&&(code==114||code==115))return code==115?FontUp:FontDown;
        uint32_t key=0;
        if(code>=16&&code<=25) key="qwertyuiop"[code-16];
        else if(code>=30&&code<=38) key="asdfghjkl"[code-30];
        else if(code>=44&&code<=50) key="zxcvbnm"[code-44];
        else if(code>=2&&code<=11) key="1234567890"[code-2];
        else switch(code) {
            case 57:key=' ';break; case 28:key=10;break;
            case 111:key=8;break; // top-right: physical Backspace
            case 14:case 1:key=Back;break; // long middle key: Back
            case 116:key=Home;break;case 0x19a:key=Symbol;break;
            case 103:key=17;break;case 108:key=18;break;
            case 106:key=19;break;case 105:key=20;break;
            default:break;
        }
        if((held_[0]||held_[1])&&key>='a'&&key<='z') {
            const char *letters="qwertyuiopasdfghjklzxcvbnm";
            const char *symbols="1234567890~@#$%&*().-/?:;,";
            for(unsigned i=0;letters[i];++i) if(key==unsigned(letters[i])) { key=symbols[i]; break; }
        } else if(caps_&&key>='a'&&key<='z') key+='A'-'a';
        return key;
    }
};
}
