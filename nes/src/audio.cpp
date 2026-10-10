#include "audio.hpp"
#include <tinyalsa/asoundlib.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace nes_audio {
namespace {
pcm *device=nullptr;
mixer *controls=nullptr;
mixer_ctl *volume_ctl=nullptr;
Mixer mix;
bool volume_warning=false;
int minimum=0,maximum=255;

bool read_volume() {
    long values[2]={0,0}; // tinyalsa INTEGER arrays use native long, not int.
    bool ok=volume_ctl&&mixer_ctl_get_array(volume_ctl,values,2)==0;
    if(!ok){
        values[0]=values[1]=minimum;
        if(!volume_warning)std::fprintf(stderr,"NES: system volume unavailable; muting audio\n");
    }
    volume_warning=!ok;mix.volume(values[0],values[1],minimum,maximum);return ok;
}
void speaker_route() {
    static const struct {const char *name,*value;} route[]={
        {"aw87xxx_profile_switch_0","Music"},{"SPKPA_L","Switch"},{"SPKPA_R","Switch"}
    };
    for(const auto &r:route){
        auto *c=mixer_get_ctl_by_name(controls,r.name);
        if(!c||mixer_ctl_set_enum_by_string(c,r.value))std::fprintf(stderr,"NES: speaker route unavailable: %s\n",r.name);
    }
}
bool write_all(int16_t *data,unsigned frames) {
    unsigned offset=0,interrupted=0,restarts=0;
    while(offset<frames) {
        errno=0;int n=pcm_writei(device,data+offset*2,frames-offset);int error=errno;
        if(n>0&&unsigned(n)<=frames-offset){offset+=unsigned(n);interrupted=0;continue;}
        if(n<0&&error==EINTR&&++interrupted<=8)continue;
        if(n<0&&(error==EPIPE||error==ESTRPIPE)&&restarts++==0&&pcm_prepare(device)==0){
            // Bound recovery and ramp the restart so an underrun does not
            // resume at an arbitrary non-zero sample with a sharp edge.
            unsigned fade=std::min(128u,frames-offset);
            for(unsigned i=0;i<fade;i++)for(unsigned c=0;c<2;c++)
                data[(offset+i)*2+c]=int16_t(int(data[(offset+i)*2+c])*int(i+1)/int(fade));
            std::fprintf(stderr,"NES: recovered PCM underrun\n");continue;
        }
        std::fprintf(stderr,"NES: PCM write failed (%d/%d): %s; continuing silently\n",n,error,pcm_get_error(device));
        return false;
    }
    return true;
}
}
void close(){
    if(device)pcm_close(device);
    device=nullptr;
    if(controls)mixer_close(controls);
    controls=nullptr;volume_ctl=nullptr;
}
bool active(){return device!=nullptr;}
bool open(int samples_per_sync,int rate){
    close();mix.reset(rate);volume_warning=false;
    if(samples_per_sync<=0||rate<8000||rate>96000)return false;
    controls=mixer_open(0);volume_ctl=controls?mixer_get_ctl_by_name(controls,"softvolume"):nullptr;
    if(!volume_ctl||mixer_ctl_get_type(volume_ctl)!=MIXER_CTL_TYPE_INT||mixer_ctl_get_num_values(volume_ctl)!=2){
        std::fprintf(stderr,"NES: stereo system volume missing; continuing silently\n");close();return false;
    }
    minimum=mixer_ctl_get_range_min(volume_ctl);maximum=mixer_ctl_get_range_max(volume_ctl);
    if(maximum<=minimum||!read_volume()){close();return false;}
    static const struct {unsigned size,count;} candidates[]={{1280,4},{1280,8},{1024,8},{1024,4},{2048,4}};
    for(const auto &p:candidates){
        pcm_config cfg{};cfg.channels=2;cfg.rate=unsigned(rate);cfg.format=PCM_FORMAT_S16_LE;
        cfg.period_size=p.size;cfg.period_count=p.count;cfg.start_threshold=p.size*2;
        // Handle errors here rather than tinyalsa retrying indefinitely.
        auto *candidate=pcm_open(0,0,PCM_OUT|PCM_NORESTART,&cfg);
        if(candidate&&pcm_is_ready(candidate)){
            device=candidate;speaker_route();
            std::fprintf(stderr,"NES: PCM %d Hz, period %ux%u, software system volume\n",rate,p.size,p.count);return true;
        }
        if(candidate){std::fprintf(stderr,"NES: PCM %ux%u unavailable: %s\n",p.size,p.count,pcm_get_error(candidate));pcm_close(candidate);}
    }
    std::fprintf(stderr,"NES: no PCM available; continuing silently\n");close();return false;
}
void output(int samples,const int16_t *mono){
    if(samples<=0||!mono||!device)return;
    read_volume();
    int16_t buffer[1024*2];
    for(int offset=0;offset<samples&&device;){
        int count=std::min(1024,samples-offset);
        for(int i=0;i<count;i++)mix.sample(mono[offset+i],buffer+i*2);
        if(!write_all(buffer,unsigned(count)))close();
        offset+=count;
    }
}
}
