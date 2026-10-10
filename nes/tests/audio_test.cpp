#include "audio.hpp"
#include <tinyalsa/asoundlib.h>
#include <array>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

// Exercise the production mixer/output code with a scripted PCM and mixer.
// No speakers, sound server, ROM or system mixer settings are touched.
struct pcm{bool ready;};struct mixer{};struct mixer_ctl{};
static mixer fake_mixer;static mixer_ctl fake_control;
static bool mixer_exists=true,control_exists=true,read_ok=true,prepare_ok=true;
static unsigned opens=0,closes=0,prepares=0,fail_opens=0,reads=0;
static long volumes[2]={255,255};
static std::deque<int> writes;
static std::vector<int16_t> captured;
static std::vector<int16_t> cast_captured;
static bool cast_local=true;
extern "C" {
struct mixer *mixer_open(unsigned int){return mixer_exists?&fake_mixer:nullptr;}
void mixer_close(struct mixer*){}
struct mixer_ctl *mixer_get_ctl_by_name(struct mixer*,const char*){return control_exists?&fake_control:nullptr;}
enum mixer_ctl_type mixer_ctl_get_type(const struct mixer_ctl*){return MIXER_CTL_TYPE_INT;}
unsigned int mixer_ctl_get_num_values(const struct mixer_ctl*){return 2;}
int mixer_ctl_get_range_min(const struct mixer_ctl*){return 0;}
int mixer_ctl_get_range_max(const struct mixer_ctl*){return 255;}
int mixer_ctl_get_array(const struct mixer_ctl*,void *data,size_t count){
    ++reads;assert(count==2);if(!read_ok)return -1;std::memcpy(data,volumes,sizeof volumes);return 0;
}
int mixer_ctl_set_enum_by_string(struct mixer_ctl*,const char *v){assert(std::strcmp(v,"Music")==0||std::strcmp(v,"Switch")==0);return 0;}
struct pcm *pcm_open(unsigned int card,unsigned int device,unsigned int flags,const struct pcm_config *cfg){
    assert(card==0&&device==0&&(flags&PCM_NORESTART));
    assert(cfg->channels==2&&cfg->format==PCM_FORMAT_S16_LE&&cfg->rate==44100);
    assert(cfg->start_threshold==cfg->period_size*2&&cfg->period_count>=4);
    return new pcm{++opens>fail_opens};
}
int pcm_is_ready(const struct pcm *p){return p->ready;}
const char *pcm_get_error(const struct pcm*){return "scripted PCM";}
int pcm_close(struct pcm *p){++closes;delete p;return 0;}
int pcm_prepare(struct pcm*){++prepares;return prepare_ok?0:-1;}
int pcm_writei(struct pcm*,const void *data,unsigned frames){
    int n=int(frames);if(!writes.empty()){n=writes.front();writes.pop_front();}
    if(n<0){errno=-n;return -1;}
    n=std::min(n,int(frames));const auto *s=static_cast<const int16_t*>(data);captured.insert(captured.end(),s,s+n*2);return n;
}
}
static void reset(){
    nes_audio::close();mixer_exists=control_exists=read_ok=prepare_ok=true;
    opens=closes=prepares=fail_opens=reads=0;volumes[0]=volumes[1]=255;writes.clear();captured.clear();
}
using Waves=std::vector<int16_t>;
static Waves signal(int count){
    Waves w(count);for(int i=0;i<count;i++)w[i]=int16_t((i%100<50?9000:-9000)+(i%64<16?3000:0));return w;
}
static void send(Waves &w){nes_audio::output(int(w.size()),w.data());}
static double energy(const std::vector<int16_t> &v,int channel=0){
    double sum=0;for(size_t i=1000+channel;i<v.size();i+=2)sum+=double(v[i])*v[i];return sum;
}
static void mixing(){
    nes_audio::Mixer m;m.reset(44100);m.volume(255,255,0,255);int16_t s[2];
    // Old code emitted -30720 for these same five silent channels.
    for(int i=0;i<10000;i++){m.sample(0,s);assert(s[0]==0&&s[1]==0);}
    int peak=0;
    for(int i=0;i<44100;i++){m.sample(i%100<50?25500:-25500,s);assert(s[0]==s[1]);peak=std::max(peak,std::abs(int(s[0])));}
    assert(peak==25500);
    m.volume(0,0,0,255);for(int i=0;i<500;i++)m.sample(i%2?-25500:25500,s);assert(s[0]==0&&s[1]==0);
    m.volume(255,0,0,255);for(int i=0;i<500;i++)m.sample(i%2?-25500:25500,s);assert(s[0]!=0&&s[1]==0);
    nes_audio::Mixer whole,split;whole.reset(44100);split.reset(44100);whole.volume(100,200,0,255);split.volume(100,200,0,255);
    std::vector<int16_t>a,b;
    for(int i=0;i<4096;i++){whole.sample(unsigned(i%1276),s);a.insert(a.end(),s,s+2);}
    for(int base=0;base<4096;base+=128)for(int i=base;i<base+128;i++){split.sample(unsigned(i%1276),s);b.insert(b.end(),s,s+2);}
    assert(a==b);
    // A volume change must ramp, not jump by thousands of PCM units. Compare
    // two identical waveforms; only one of the mixers receives a mute command.
    nes_audio::Mixer playing,muting;playing.reset(44100);muting.reset(44100);
    playing.volume(255,255,0,255);muting.volume(255,255,0,255);
    int16_t reference[2],faded[2];
    for(int i=0;i<2000;i++){playing.sample(i%2?25500:-25500,reference);muting.sample(i%2?25500:-25500,faded);}
    muting.volume(0,0,0,255);playing.sample(25500,reference);muting.sample(25500,faded);
    assert(std::abs(int(reference[0])-faded[0])<128);
    for(int i=1;i<220;i++)muting.sample(i%2?-25500:25500,faded);
    assert(faded[0]==0&&faded[1]==0);
}
static void io(){
    reset();fail_opens=1;assert(nes_audio::open(735,44100)&&opens==2&&closes==1);
    auto w=signal(4096);auto original=w;
    writes={7,-EINTR,200,1};send(w);assert(captured.size()==8192&&reads==2&&nes_audio::active());
    assert(w==original);
    auto partial=captured;reset();assert(nes_audio::open(735,44100));send(original);assert(partial==captured);
    // Live system volume is sampled every callback. Independent L/R and mute
    // change PCM samples without writing the global volume control twice.
    reset();assert(nes_audio::open(735,44100));w=signal(4096);send(w);auto full=energy(captured);
    reset();volumes[0]=128;volumes[1]=0;assert(nes_audio::open(735,44100));w=signal(4096);send(w);
    assert(energy(captured)>full*.23&&energy(captured)<full*.28&&energy(captured,1)==0);
    volumes[0]=0;captured.clear();w=signal(4096);send(w);assert(energy(captured)==0);
    volumes[0]=255;captured.clear();w=signal(4096);send(w);assert(energy(captured)>0);
    read_ok=false;captured.clear();w=signal(4096);send(w);assert(energy(captured)==0&&nes_audio::active());
    read_ok=true;captured.clear();w=signal(4096);send(w);assert(energy(captured)>0);
    reset();assert(nes_audio::open(735,44100));writes={-EPIPE};w=signal(735);send(w);assert(prepares==1&&captured.size()==1470&&nes_audio::active());
    reset();assert(nes_audio::open(735,44100));writes={-EPIPE,-EPIPE};w=signal(735);send(w);assert(prepares==1&&!nes_audio::active()&&closes==1);
    reset();assert(nes_audio::open(735,44100));prepare_ok=false;writes={-EPIPE};w=signal(735);send(w);assert(prepares==1&&!nes_audio::active());
    for(int error:{0,-EIO}){reset();assert(nes_audio::open(735,44100));writes={error};w=signal(735);send(w);assert(!nes_audio::active()&&closes==1);}
    reset();assert(nes_audio::open(735,44100));writes.assign(9,-EINTR);w=signal(735);send(w);assert(!nes_audio::active());
    reset();fail_opens=99;assert(!nes_audio::open(735,44100)&&opens==5&&closes==5);
    for(int failure=0;failure<3;failure++){reset();if(failure==0)mixer_exists=false;if(failure==1)control_exists=false;if(failure==2)read_ok=false;assert(!nes_audio::open(735,44100)&&opens==0);}
    reset();assert(!nes_audio::open(0,44100)&&!nes_audio::open(735,-1));
    nes_audio::output(-1,nullptr);
}
static void pacing(){
    nes_audio::FrameClock c;uint64_t now=1000000,start=now;
    for(int frame=0;frame<60;frame++){now+=3000;auto delay=c.delay(now);assert(delay<=16667);now+=delay;}
    assert(now-start>=1000000&&now-start<1010000);
    now+=1000000;assert(c.delay(now)==16667);c.reset();assert(c.delay(now)==16667);
    // Broken PCM accepts every frame immediately. The independent wall clock
    // still holds 60 frames to one second, rather than racing the CPU.
    c.reset();now=1000000;start=now;
    for(int i=0;i<60;i++){now+=100;now+=c.delay(now);}
    assert(now-start>=1000000&&now-start<1001000);
    // Healthy PCM blocks slightly longer than a frame: never add 16 ms to
    // every blocking write, which would accidentally halve the game speed.
    c.reset();now=1000000;start=now;
    for(int i=0;i<60;i++){now+=17000;now+=c.delay(now);}
    assert(now-start>=1020000&&now-start<1040000);
    // InfoNES advances 263 * 113 CPU clocks per frame. Keep the wall-clock
    // limit aligned with the APU rate instead of slowly starving a 44.1k DAC.
    c.reset();now=1000000;start=now;
    constexpr unsigned interval=uint64_t(263)*113*1000000/1789773;
    for(int i=0;i<6000;i++){now+=100;now+=c.delay(now,interval);}
    const double generated=6000.0*263*113*44100/1789773;
    const double consumed=double(now-start)*44100/1000000;
    assert(generated>=consumed&&generated-consumed<1000);
}
static void resume(){
    reset();assert(nes_audio::open(735,44100));nes_audio::service(1000000);
    auto w=signal(735);writes={-EIO};send(w);assert(!nes_audio::active());
    // Two seconds of healthy frame callbacks, no busy-loop reopen attempts.
    for(uint64_t t=1010000;t<3000000;t+=10000)nes_audio::service(t);
    assert(opens==1);nes_audio::service(3000000);assert(opens==2&&nes_audio::active());
    captured.clear();send(w);assert(!captured.empty());
    // A suspend gap also recreates a handle that still looks ready/healthy.
    nes_audio::service(23000000);assert(opens==3&&closes==2&&nes_audio::active());
    volumes[0]=volumes[1]=0;captured.clear();send(w);
    for(auto value:captured)assert(value==0); // Preserve mute across resume.
    nes_audio::close();nes_audio::service(25000000);assert(opens==3&&!nes_audio::active());
    reset();fail_opens=99;assert(!nes_audio::open(735,44100));nes_audio::service(1000000);
    unsigned failed=opens;
    for(uint64_t t=1010000;t<3000000;t+=10000)nes_audio::service(t);
    assert(opens==failed);fail_opens=0;nes_audio::service(3000000);assert(nes_audio::active());
}
static void casting(){
    reset();cast_local=true;cast_captured.clear();
    nes_audio::cast_hooks([](const int16_t *data,size_t frames,unsigned channels,unsigned rate){assert(channels==1&&rate==44100);cast_captured.insert(cast_captured.end(),data,data+frames);},[](){return cast_local;});
    assert(nes_audio::open(735,44100));auto wave=signal(4096);send(wave);assert(cast_captured==wave&&energy(captured)>0);
    cast_local=false;captured.clear();cast_captured.clear();send(wave);
    assert(cast_captured==wave&&captured.size()==wave.size()*2&&energy(captured)==0); // PCM clock still receives frames.
    cast_local=true;volumes[0]=128;captured.clear();send(wave);assert(energy(captured)>0&&energy(captured)<energy(captured,1)*.3);
    cast_local=false;writes={-EIO};send(wave);assert(!nes_audio::active());cast_captured.clear();send(wave);assert(cast_captured==wave); // A failed local DAC must not cut the remote stream.
    nes_audio::cast_hooks(nullptr,nullptr);reset();
}
int main(){mixing();io();pacing();resume();casting();reset();std::puts("PASS NES: PCM/volume, bounded recovery, resume/retry/mute, cast fade/capture and independent VBlank clock");}
