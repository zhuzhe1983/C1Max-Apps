#pragma once
#include "libretro.h"
#include "../../shared/keymap.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <cstring>
namespace dos {
class Input {
    struct Chord {unsigned key=0,modifier=0;};
    std::array<Chord,512> active_{};
    std::array<unsigned,RETROK_LAST> held_{};
    keyboard::Keymap mapper_;
    uint64_t camera_at_=0;
    bool camera_down_=false,game_=false;
    int prefix_=0;
    void ref(unsigned key,bool down){if(!key||key>=held_.size())return;if(down){if(!held_[key]++)send(true,key);}else if(held_[key]&&!--held_[key])send(false,key);}
    void release(unsigned code){auto a=active_[code];active_[code]={};ref(a.key,false);ref(a.modifier,false);}
    static Chord ascii(uint32_t c){
        if(c>='A'&&c<='Z')return {c+32,RETROK_LSHIFT};
        const char *shifted="!@#$%^&*()_+{}|:\"<>?~",*base="1234567890-=[]\\;',./`";
        if(c>=32&&c<127)if(auto *p=strchr(shifted,char(c)))return {unsigned(base[p-shifted]),RETROK_LSHIFT};
        return c>=32&&c<127?Chord{c,0}:Chord{};
    }
public:
    enum Action {None,Hint,Menu,Home};
    std::function<void(bool,unsigned)> send=[](bool,unsigned){};
    bool down(unsigned key)const{return key<held_.size()&&held_[key];}
    bool game()const{return game_;}
    bool caps()const{return mapper_.caps_lock();}
    void set_game(bool game){clear();game_=game;}
    int prefix()const{return prefix_;}
    const char *hint()const{
        switch(prefix_){
        case 1:return "NAV: WASD arrows, QE Home/End, ZX PgUp/Dn, B Del, Space Tab";
        case 2:return "FN: Q W E R T Y U I O P A S = F1..F12";
        case 3:return "CTRL: next letter (hold letter to hold the chord)";
        case 4:return "ALT: next letter (hold letter to hold the chord)";
        case 5:return "SYM: Q[ W] E{ R} T< Y> U= I+ O_ P\\ A\" S' D` F! G| H^";
        default:return game_?"GAME: WASD arrows, J Ctrl, K Alt, U Space, I Enter, Shift":(mapper_.caps_lock()?"TEXT [CAPS ON]: Shift symbols; double Shift for lowercase; Back = Esc":"TEXT [abc]: Shift symbols; double Shift CAPS; Back = Esc");
        }
    }
    void clear(){for(unsigned c=0;c<active_.size();c++)release(c);mapper_.lost_events();prefix_=0;camera_down_=false;}
    Action event(unsigned code,int value,uint64_t ms){
        if(code>=active_.size())return None;
        if(code==116)return value==1?Home:None;
        if(code==410){if(value==1){camera_at_=ms;camera_down_=true;}else if(value==0&&camera_down_){camera_down_=false;if(ms>=camera_at_&&ms-camera_at_>=650){clear();return Menu;}prefix_=(prefix_+1)%6;return Hint;}return None;}
        auto mapped=mapper_.event(code,value,ms);
        if(!value){release(code);return mapped==keyboard::Mode?Hint:None;}
        if(value!=1||active_[code].key)return None;
        if((code==42||code==54)&&!game_)return None;
        if(mapped==keyboard::Mode)return Hint;
        if(!mapped&&code!=42&&code!=54)return None;
        if(game_&&!prefix_){
            if(code>=16&&code<=25)mapped="qwertyuiop"[code-16];
            else if(code>=30&&code<=38)mapped="asdfghjkl"[code-30];
            else if(code>=44&&code<=50)mapped="zxcvbnm"[code-44];
        }
        if(mapped==keyboard::Back&&prefix_){prefix_=0;return Hint;}
        Chord a;unsigned lower=mapped>='A'&&mapped<='Z'?mapped+32:mapped;int prefix=prefix_;prefix_=0;
        if(prefix==1){switch(lower){case 'w':a.key=RETROK_UP;break;case 'a':a.key=RETROK_LEFT;break;case 's':a.key=RETROK_DOWN;break;case 'd':a.key=RETROK_RIGHT;break;case 'q':a.key=RETROK_HOME;break;case 'e':a.key=RETROK_END;break;case 'z':a.key=RETROK_PAGEUP;break;case 'x':a.key=RETROK_PAGEDOWN;break;case 'b':a.key=RETROK_DELETE;break;case ' ':a.key=RETROK_TAB;break;}}
        else if(prefix==2){const char *keys="qwertyuiopas";if(lower>=32&&lower<127)if(auto *p=strchr(keys,char(lower)))a.key=RETROK_F1+unsigned(p-keys);}
        else if(prefix==3||prefix==4){a=ascii(lower);if(a.key)a.modifier=prefix==3?RETROK_LCTRL:RETROK_LALT;}
        else if(prefix==5){const char *keys="qwertyuiopasdfgh",*values="[]{}<>=+_\\\"'`!|^";if(lower>=32&&lower<127)if(auto *p=strchr(keys,char(lower)))a=ascii(values[p-keys]);}
        else if(code==111)a.key=RETROK_BACKSPACE;
        else if(code==14||code==1)a.key=RETROK_ESCAPE;
        else if(code==28)a.key=RETROK_RETURN;
        else if(game_){switch(code){case 17:a.key=RETROK_UP;break;case 30:a.key=RETROK_LEFT;break;case 31:a.key=RETROK_DOWN;break;case 32:a.key=RETROK_RIGHT;break;case 36:a.key=RETROK_LCTRL;break;case 37:a.key=RETROK_LALT;break;case 22:a.key=RETROK_SPACE;break;case 23:a.key=RETROK_RETURN;break;case 42:case 54:a.key=RETROK_LSHIFT;break;default:a=ascii(lower);}}
        else if(mapped==17)a.key=RETROK_UP;else if(mapped==18)a.key=RETROK_DOWN;else if(mapped==19)a.key=RETROK_RIGHT;else if(mapped==20)a.key=RETROK_LEFT;
        else a=ascii(mapped);
        if(a.key){active_[code]=a;ref(a.modifier,true);ref(a.key,true);}return prefix?Hint:None;
    }
};
}
