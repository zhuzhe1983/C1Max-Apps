#pragma once
#include <cstdint>

namespace keyboard {
// Shared power-key state machine for emulator input backends.
class PowerHold {
public:
    enum Action { None, Hint, Exit };
    static constexpr uint64_t HoldMs=5000;

    Action event(int value,uint64_t ms) {
        if(value==1) {
            if(held_)return None;
            held_=true;exited_=false;pressed_at_=ms;
            return Hint;
        }
        if(value==0&&held_) {
            const bool long_enough=ms>=pressed_at_&&ms-pressed_at_>=HoldMs;
            held_=false;
            if(long_enough&&!exited_){exited_=true;return Exit;}
        }
        return None;
    }
    Action tick(uint64_t ms) {
        if(held_&&!exited_&&ms>=pressed_at_&&ms-pressed_at_>=HoldMs) {
            exited_=true;
            return Exit;
        }
        return None;
    }
    void reset(){held_=false;exited_=false;pressed_at_=0;}
private:
    bool held_=false,exited_=false;
    uint64_t pressed_at_=0;
};
}
