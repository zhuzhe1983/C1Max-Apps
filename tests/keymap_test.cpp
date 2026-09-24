#include "../shared/keymap.hpp"
#include <cassert>
#include <cstdio>
int main(){
    keyboard::Keymap k;
    assert(k.event(111,1,0)==8);assert(k.event(14,1,1)==keyboard::Back);
    assert(k.event(116,1,2)==keyboard::Home);assert(k.event(116,0,3)==0);
    assert(k.tick(5001)==0); // a quick tap never triggers the long action
    assert(k.event(116,1,6000)==keyboard::Home);assert(k.event(116,2,10999)==0);
    assert(k.tick(10999)==0);assert(k.tick(11000)==keyboard::HomeLong);
    assert(k.tick(16000)==0);assert(k.event(116,0,16001)==0);
    assert(k.event(116,1,17000)==keyboard::Home);k.lost_events();assert(k.tick(23000)==0);
    assert(k.event(116,1,24000)==keyboard::Home);assert(k.event(116,0,29000)==keyboard::HomeLong);
    assert(k.event(28,1,30000)==10);
    assert(k.event(16,1,4)=='q');assert(k.event(16,0,5)==0);
    k.event(42,1,10);assert(k.event(16,1,11)=='1');k.event(42,0,12);
    k.event(42,1,20);assert(k.event(42,0,30)==0); // previous chord was not a tap
    k.event(42,1,50);assert(k.event(42,0,60)==keyboard::Mode);
    assert(k.caps_lock());assert(k.event(16,1,70)=='Q');assert(k.event(16,2,71)=='Q');
    k.event(42,1,80);assert(k.event(16,1,81)=='1');k.event(42,0,82);
    k.event(42,1,90);k.event(42,0,100);k.event(30,1,110);
    k.event(42,1,120);assert(k.event(42,0,130)==0);assert(k.caps_lock());
    k.event(42,1,140);assert(k.event(42,0,150)==keyboard::Mode);assert(!k.caps_lock());
    k.reset();k.event(42,1,1);k.event(42,0,800);k.event(42,1,900);
    assert(k.event(42,0,1000)==0);assert(!k.caps_lock());
    k.reset();k.event(42,1,1);k.lost_events();assert(k.event(16,1,3)=='q');
    const unsigned codes[]={16,17,18,19,20,21,22,23,24,25,30,31,32,33,34,35,36,37,38,44,45,46,47,48,49,50};
    const char *symbols="1234567890~@#$%&*().-/?:;,";
    k.event(42,1,100);for(unsigned i=0;i<26;i++)assert(k.event(codes[i],1,101+i)==unsigned(symbols[i]));
    k.event(54,1,150);k.event(42,0,151);assert(k.event(46,1,152)=='/');k.event(54,0,153);
    assert(k.event(46,1,154)=='c');assert(k.event(0x19a,1,155)==keyboard::Symbol);
    k.reset();assert(k.event(115,1,1)==0);k.event(42,1,2);
    assert(k.event(115,1,3)==keyboard::FontUp);assert(k.event(114,2,4)==keyboard::FontDown);
    assert(k.event(42,0,5)==0);k.event(42,1,6);assert(k.event(42,0,7)==0);assert(!k.caps_lock());
    puts("keymap: right-side roles, caps latch, Shift symbols, repeat and event loss PASS");
}
