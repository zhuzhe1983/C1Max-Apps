#include "../src/platform.hpp"
#include <cassert>
#include <cstdio>
int main(){
    // Never opens a framebuffer or input device. Test row padding, updates,
    // and rejection of invalid images without losing the cached valid image.
    dos::Platform p;
    uint32_t rows[8]={1,2,99,99,3,4,99,99};
    assert(p.frame(rows,2,2,16));
    assert(!p.frame(rows,2,2,16));
    rows[2]=101;assert(!p.frame(rows,2,2,16));
    rows[5]=5;assert(p.frame(rows,2,2,16));
    assert(!p.frame(nullptr,2,2,16));
    assert(!p.frame(rows,2,2,4));
    assert(!p.frame(rows,1025,1,4100));
    assert(!p.frame(rows,2,2,16));
    assert(p.frame(rows,3,2,16));
    assert(!p.frame(rows,3,2,16));
    puts("PASS DOS video cache: unchanged pixels, row padding, updates, dimensions and invalid frames");
}
