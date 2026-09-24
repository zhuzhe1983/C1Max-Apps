#include "../src/input.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
int main(){
    dos::Input in;std::vector<std::pair<bool,unsigned>> events;in.send=[&](bool d,unsigned k){events.emplace_back(d,k);};uint64_t now=1000;
    auto press=[&](int c){in.event(c,1,now+=30);};auto release=[&](int c){in.event(c,0,now+=30);};auto tap=[&](int c){press(c);release(c);};
    press(42);press(16);assert(in.down(RETROK_1)&&!in.down(RETROK_q));release(42);assert(in.down(RETROK_1));release(16);
    tap(42);tap(42);assert(in.caps());press(30);assert(in.down(RETROK_a)&&in.down(RETROK_LSHIFT));release(30);assert(!in.down(RETROK_LSHIFT));tap(42);tap(42);assert(!in.caps());
    press(111);assert(in.down(RETROK_BACKSPACE));release(111);press(14);assert(in.down(RETROK_ESCAPE)&&!in.down(RETROK_BACKSPACE));release(14);press(28);assert(in.down(RETROK_RETURN));release(28);
    tap(410);press(17);assert(in.down(RETROK_UP));release(17);assert(!in.down(RETROK_UP));
    tap(410);tap(410);press(30);assert(in.down(RETROK_F11));release(30);
    tap(410);tap(410);tap(410);press(46);assert(in.down(RETROK_LCTRL)&&in.down(RETROK_c));release(46);assert(!in.down(RETROK_LCTRL));
    for(int n=0;n<4;n++)tap(410);tap(114);assert(in.prefix()==4);press(33);assert(in.down(RETROK_LALT)&&in.down(RETROK_f));release(33);assert(!in.down(RETROK_LALT));
    in.set_game(true);press(17);press(36);assert(in.down(RETROK_UP)&&in.down(RETROK_LCTRL));in.clear();assert(!in.down(RETROK_UP)&&!in.down(RETROK_LCTRL));
    press(42);assert(in.down(RETROK_LSHIFT));release(42);assert(!in.down(RETROK_LSHIFT));
    press(42);press(16);assert(in.down(RETROK_q)&&in.down(RETROK_LSHIFT)&&!in.down(RETROK_1));release(16);release(42);
    in.set_game(false);for(int n=0;n<5;n++)tap(410);press(18);assert(in.down(RETROK_LEFTBRACKET)&&in.down(RETROK_LSHIFT));release(18);assert(!in.down(RETROK_LSHIFT));
    for(int n=0;n<5;n++)tap(410);press(21);assert(in.down(RETROK_PERIOD)&&in.down(RETROK_LSHIFT));release(21);
    for(int n=0;n<5;n++)tap(410);press(20);assert(in.down(RETROK_COMMA)&&in.down(RETROK_LSHIFT));release(20);
    tap(410);assert(in.prefix()==1);tap(14);assert(in.prefix()==0&&!in.down(RETROK_ESCAPE));
    in.event(410,1,now);assert(in.event(410,0,now+800)==dos::Input::Menu);
    uint64_t power=now+900;assert(in.event(116,1,power)==dos::Input::Hint);assert(in.tick(power+4999)==dos::Input::None);assert(in.tick(power+5000)==dos::Input::Home);
    assert(in.event(116,0,power+5001)==dos::Input::None);power+=6000;
    assert(in.event(116,1,power)==dos::Input::Hint);assert(in.event(116,0,power+5000)==dos::Input::Home);
    puts("PASS DOS held keys, Shift numbers/caps, Backspace/Esc/Enter, prefixes, game mode and releases");
}
