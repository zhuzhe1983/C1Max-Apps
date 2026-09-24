#include "keyboard.hpp"
#include "keymap.hpp"
#include "lvgl.h"
#include <linux/input.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <deque>
#include <vector>
#include <algorithm>
namespace keyboard {
static_assert(LV_KEY_BACKSPACE==8 && LV_KEY_ENTER==10 && KEY_SHUFFLE==0x19a);
static int fds[2]={-1,-1};
static bool dropped[2]={false,false};
static clockid_t event_clock=CLOCK_REALTIME;
static Keymap mapper;
static std::deque<uint32_t> queue;
static uint64_t clock_ms(){timespec t{};clock_gettime(event_clock,&t);return uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
void close(){for(auto&fd:fds){if(fd>=0)::close(fd);fd=-1;}queue.clear();mapper.reset();dropped[0]=dropped[1]=false;event_clock=CLOCK_REALTIME;}
void open(){
    close();fds[0]=::open("/dev/input/event0",O_RDONLY|O_NONBLOCK|O_CLOEXEC);fds[1]=::open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    bool monotonic=true;
#ifdef EVIOCSCLOCKID
    clockid_t wanted=CLOCK_MONOTONIC;
    for(auto fd:fds)if(fd>=0&&ioctl(fd,EVIOCSCLOCKID,&wanted)<0)monotonic=false;
    if(!monotonic){wanted=CLOCK_REALTIME;for(auto fd:fds)if(fd>=0)ioctl(fd,EVIOCSCLOCKID,&wanted);}
#else
    monotonic=false;
#endif
    event_clock=monotonic?CLOCK_MONOTONIC:CLOCK_REALTIME;
    input_event e;for(auto fd:fds)while(fd>=0&&read(fd,&e,sizeof(e))==sizeof(e)){}
}
bool caps_lock(){return mapper.caps_lock();}
void poll(){
    struct Item{input_event e;int device;};std::vector<Item> events;
    // Merge both event devices by timestamp. A Shift release on event0 must not
    // overtake a letter that was pressed earlier on event1.
    for(int i=0;i<2;i++){input_event e;int count=0;while(fds[i]>=0&&count++<256&&read(fds[i],&e,sizeof e)==sizeof e)events.push_back({e,i});}
    std::stable_sort(events.begin(),events.end(),[](const Item&a,const Item&b){return a.e.time.tv_sec!=b.e.time.tv_sec?a.e.time.tv_sec<b.e.time.tv_sec:a.e.time.tv_usec<b.e.time.tv_usec;});
    for(const auto&item:events){const auto&e=item.e;int i=item.device;
        if(e.type==EV_SYN&&e.code==SYN_DROPPED){dropped[i]=true;mapper.lost_events();queue.clear();continue;}
        if(dropped[i]){if(e.type==EV_SYN&&e.code==SYN_REPORT)dropped[i]=false;continue;}
        if(e.type!=EV_KEY)continue;
        auto key=mapper.event(e.code,e.value,uint64_t(e.time.tv_sec)*1000+e.time.tv_usec/1000);
        if(key){if(queue.size()>=64)queue.pop_front();queue.push_back(key);}
    }
    auto key=mapper.tick(clock_ms());
    if(key){if(queue.size()>=64)queue.pop_front();queue.push_back(key);}
}
uint32_t take(){poll();if(queue.empty())return 0;auto k=queue.front();queue.pop_front();return k;}
}
