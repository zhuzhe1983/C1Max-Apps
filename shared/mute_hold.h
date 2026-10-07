#ifndef C1_MUTE_HOLD_H
#define C1_MUTE_HOLD_H
#include <stdint.h>
#define C1_MUTE_HOLD_MS 3000u
struct c1_mute_hold { unsigned down; uint64_t since; int blocked, fired; };
static inline void c1_mute_reset(struct c1_mute_hold *s) {
    s->down=0; s->since=0; s->blocked=0; s->fired=0;
}
static inline void c1_mute_key(struct c1_mute_hold *s,int device,int value,uint64_t now,int shifted) {
    if(device<0||device>1||(value!=0&&value!=1))return;
    unsigned bit=1u<<device;
    if(!value){s->down&=~bit;if(!s->down)c1_mute_reset(s);return;}
    if(s->down&bit)return;
    if(!s->down){s->since=now;s->fired=0;s->blocked=0;}
    s->down|=bit;if(shifted)s->blocked=1;
}
static inline int c1_mute_due(struct c1_mute_hold *s,uint64_t now) {
    if(!s->down||s->blocked||s->fired||now<s->since||now-s->since<C1_MUTE_HOLD_MS)return 0;
    s->fired=1;return 1;
}
static inline int c1_mute_timeout(const struct c1_mute_hold *s,uint64_t now,int fallback) {
    if(!s->down||s->blocked||s->fired)return fallback;
    uint64_t elapsed=now>=s->since?now-s->since:0;
    int remaining=elapsed>=C1_MUTE_HOLD_MS?0:(int)(C1_MUTE_HOLD_MS-elapsed);
    return remaining<fallback?remaining:fallback;
}
#endif
