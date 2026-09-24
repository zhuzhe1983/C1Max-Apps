#include "out.h"
#include "tinyalsa/asoundlib.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
extern int c1_psx_mute;
static struct pcm *device;
/* Keep PCM back-pressure away from the emulator/vblank thread. The SPU buffer
   itself is 32 KiB, so every feed callback fits in one bounded queue slot. */
#define AUDIO_QUEUE_SLOTS 8
#define AUDIO_CHUNK_BYTES 32768
static unsigned char audio_queue[AUDIO_QUEUE_SLOTS][AUDIO_CHUNK_BYTES];
static unsigned audio_sizes[AUDIO_QUEUE_SLOTS],audio_head,audio_tail,audio_count;
static pthread_mutex_t audio_mutex=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t audio_ready=PTHREAD_COND_INITIALIZER;
static pthread_t audio_thread;
static int audio_thread_started,audio_stopping,audio_failed,audio_drop_reported,audio_device_busy;
static void *audio_writer(void *unused);
static void enable_speaker(void){
    struct mixer *m=mixer_open(0);
    if(!m){fprintf(stderr,"PCSX audio mixer unavailable\n");return;}
    static const struct {const char *name,*value;} route[]={
        {"aw87xxx_profile_switch_0","Music"},
        {"SPKPA_L","Switch"},
        {"SPKPA_R","Switch"},
    };
    for(unsigned i=0;i<sizeof(route)/sizeof(route[0]);i++){
        struct mixer_ctl *control=mixer_get_ctl_by_name(m,route[i].name);
        if(!control||mixer_ctl_set_enum_by_string(control,route[i].value))
            fprintf(stderr,"PCSX audio route unavailable: %s=%s\n",route[i].name,route[i].value);
    }
    mixer_close(m);
}
static int init(void){
    if(c1_psx_mute)return 0;
    /* The stock UI powers down the speaker amp when its service exits. */
    enable_speaker();
    /* The Ingenic AS driver rejects the usual 1024x4 buffer. Match the
       period geometry used by the working piano app, trying its fallbacks. */
    static const struct {unsigned period_size,period_count;} candidates[]={
        {1280,8},{1280,4},{1024,8},{1024,4},{2048,4},{960,8},{512,8},{256,8}
    };
    struct pcm_config c={0};c.channels=2;c.rate=44100;c.format=PCM_FORMAT_S16_LE;
    for(unsigned i=0;i<sizeof(candidates)/sizeof(candidates[0]);i++){
        c.period_size=candidates[i].period_size;c.period_count=candidates[i].period_count;
        struct pcm *candidate=pcm_open(0,0,PCM_OUT,&c);
        if(candidate&&pcm_is_ready(candidate)){device=candidate;fprintf(stderr,"PCSX audio PCM ready: 44100Hz period=%ux%u\n",c.period_size,c.period_count);break;}
        if(candidate){fprintf(stderr,"PCSX audio PCM %ux%u unavailable: %s\n",
            c.period_size,c.period_count,pcm_get_error(candidate));pcm_close(candidate);}
    }
    if(!device){
        fprintf(stderr,"PCSX audio unavailable; continuing silently\n");
    }else{
        pthread_mutex_lock(&audio_mutex);audio_head=audio_tail=audio_count=0;audio_stopping=audio_failed=audio_drop_reported=audio_device_busy=0;pthread_mutex_unlock(&audio_mutex);
        int err=pthread_create(&audio_thread,0,audio_writer,0);
        if(err){fprintf(stderr,"PCSX audio thread unavailable (%d); continuing silently\n",err);pcm_close(device);device=0;}
        else audio_thread_started=1;
    }
    return 0;
}
static void *audio_writer(void *unused){
    (void)unused;
    unsigned char block[AUDIO_CHUNK_BYTES];
    for(;;){
        pthread_mutex_lock(&audio_mutex);
        while(!audio_count&&!audio_stopping)pthread_cond_wait(&audio_ready,&audio_mutex);
        if(audio_stopping){pthread_mutex_unlock(&audio_mutex);break;}
        unsigned bytes=audio_sizes[audio_head];
        memcpy(block,audio_queue[audio_head],bytes);
        audio_head=(audio_head+1)%AUDIO_QUEUE_SLOTS;--audio_count;
        pthread_mutex_unlock(&audio_mutex);
        unsigned frames=bytes/4,offset=0;
        while(offset<frames){
            int written=pcm_writei(device,block+offset*4,frames-offset);
            if(written<=0){
                fprintf(stderr,"PCSX audio output failed; continuing silently: %s\n",pcm_get_error(device));
                pthread_mutex_lock(&audio_mutex);audio_failed=1;audio_count=0;pthread_mutex_unlock(&audio_mutex);
                return 0;
            }
            offset+=(unsigned)written;
        }
        /* Match the upstream ALSA driver's busy threshold without touching
           the PCM handle concurrently with pcm_writei. */
        int available=pcm_avail_update(device);
        unsigned buffer=pcm_get_buffer_size(device);
        pthread_mutex_lock(&audio_mutex);
        audio_device_busy=available>=0&&buffer>0&&(unsigned)available<buffer/2;
        pthread_mutex_unlock(&audio_mutex);
    }
    return 0;
}
static void finish(void){
    pthread_mutex_lock(&audio_mutex);audio_stopping=1;audio_count=0;pthread_cond_broadcast(&audio_ready);pthread_mutex_unlock(&audio_mutex);
    if(audio_thread_started){pthread_join(audio_thread,0);audio_thread_started=0;}
    if(device)pcm_close(device);device=0;
}
static int busy(void){
    if(!device||!audio_thread_started)return 0;
    pthread_mutex_lock(&audio_mutex);int full=audio_device_busy||audio_count>=4;pthread_mutex_unlock(&audio_mutex);
    return full;
}
static void feed(void *data,int bytes){
    if(!device||bytes<=0||bytes%4)return;
    if(bytes>AUDIO_CHUNK_BYTES){fprintf(stderr,"PCSX audio block exceeds output queue; dropped\n");return;}
    pthread_mutex_lock(&audio_mutex);
    if(!audio_stopping&&!audio_failed){
        if(audio_count==AUDIO_QUEUE_SLOTS){audio_head=(audio_head+1)%AUDIO_QUEUE_SLOTS;--audio_count;if(!audio_drop_reported){fprintf(stderr,"PCSX audio queue overrun; dropping stale samples to keep playback current\n");audio_drop_reported=1;}}
        memcpy(audio_queue[audio_tail],data,(size_t)bytes);audio_sizes[audio_tail]=(unsigned)bytes;
        audio_tail=(audio_tail+1)%AUDIO_QUEUE_SLOTS;++audio_count;pthread_cond_signal(&audio_ready);
    }
    pthread_mutex_unlock(&audio_mutex);
}
void out_register_libretro(struct out_driver *d){d->name=c1_psx_mute?"c1max-muted":"c1max-tinyalsa";d->init=init;d->finish=finish;d->busy=busy;d->feed=feed;}
