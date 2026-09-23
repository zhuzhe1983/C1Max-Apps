#include "out.h"
#include "tinyalsa/asoundlib.h"
#include <stdio.h>
extern int c1_psx_mute;
static struct pcm *device;
static int init(void){
    if(c1_psx_mute)return 0;
    struct pcm_config c={0};c.channels=2;c.rate=44100;c.format=PCM_FORMAT_S16_LE;c.period_size=1024;c.period_count=4;
    device=pcm_open(0,0,PCM_OUT,&c);
    if(!device||!pcm_is_ready(device)){if(device)pcm_close(device);device=0;fprintf(stderr,"PCSX audio unavailable; continuing silently\n");}
    return 0;
}
static void finish(void){if(device)pcm_close(device);device=0;}
static int busy(void){return 0;}
static void feed(void *data,int bytes){if(device&&bytes>0&&pcm_writei(device,data,bytes/4)<0){fprintf(stderr,"PCSX audio output failed; muted\n");finish();}}
void out_register_libretro(struct out_driver *d){d->name=c1_psx_mute?"c1max-muted":"c1max-tinyalsa";d->init=init;d->finish=finish;d->busy=busy;d->feed=feed;}
