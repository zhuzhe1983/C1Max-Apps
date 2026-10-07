#include "../shared/mute_hold.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    struct c1_mute_hold s={0};
    c1_mute_key(&s,0,1,100,0);
    assert(!c1_mute_due(&s,3099));assert(c1_mute_timeout(&s,3099,1000)==1);
    assert(c1_mute_due(&s,3100));assert(!c1_mute_due(&s,9000));
    c1_mute_key(&s,0,0,9200,0);assert(!c1_mute_due(&s,20000));
    c1_mute_key(&s,0,1,21000,0);c1_mute_key(&s,0,2,23000,0);
    assert(c1_mute_due(&s,24000));
    c1_mute_reset(&s);c1_mute_key(&s,0,1,1,0);c1_mute_key(&s,1,1,2,0);
    c1_mute_key(&s,0,0,3,0);assert(c1_mute_due(&s,3001));
    c1_mute_reset(&s);c1_mute_key(&s,0,1,1,1);assert(!c1_mute_due(&s,9000));
    c1_mute_key(&s,0,0,9001,0);c1_mute_key(&s,0,1,10000,0);assert(c1_mute_due(&s,13000));
    c1_mute_reset(&s);c1_mute_key(&s,0,1,10,0);c1_mute_key(&s,0,0,2000,0);assert(!c1_mute_due(&s,3010));
    c1_mute_reset(&s);c1_mute_key(&s,0,2,10,0);assert(!c1_mute_due(&s,5000));
    puts("PASS mute: exact deadline before release, repeats, short press, two input devices, Shift exclusion and reset");
}
