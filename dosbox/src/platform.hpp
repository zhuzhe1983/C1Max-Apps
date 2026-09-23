// GPL-2.0-or-later. A single framebuffer owner for the DOS frontend.
#pragma once
#include "../../launcher/src/font8x8.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
namespace dos {
inline uint64_t micros(){timespec t{};clock_gettime(CLOCK_MONOTONIC,&t);return uint64_t(t.tv_sec)*1000000+t.tv_nsec/1000;}
class Platform {
    int fb_=-1,keys_[3]={-1,-1,-1},stride_=0,pages_=0;
    bool dropped_[3]={};
    uint8_t *mapped_=nullptr,*page_=nullptr;
    size_t size_=0;
    fb_var_screeninfo initial_{},current_{};
    std::vector<uint32_t> last_;
    unsigned width_=0,height_=0;
    bool dirty_=true,last_stretch_=false;
    std::string last_hint_;
    void begin(){ioctl(fb_,FBIOGET_VSCREENINFO,&current_);unsigned p=pages_>1?(current_.yoffset/800+1)%pages_:0;current_.yoffset=p*800;page_=mapped_+size_t(p)*stride_*800;}
    bool end(){__sync_synchronize();current_.xoffset=0;current_.activate=FB_ACTIVATE_VBL;return ioctl(fb_,FBIOPAN_DISPLAY,&current_)==0;}
    void pixel(int x,int y,uint32_t color){if(x>=0&&x<800&&y>=0&&y<340)*(uint32_t*)(page_+size_t(799-x)*stride_+y*4)=color|0xff000000;}
    void blank(uint32_t color){for(int x=0;x<800;x++){auto*p=(uint32_t*)(page_+size_t(799-x)*stride_);std::fill(p,p+340,color|0xff000000);}}
    void text(int x,int y,const std::string&s,uint32_t color=0xd9e5ed,int scale=2){for(unsigned char c:s){if(x+8*scale>800)break;if(c>=128)c='?';for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(font8x8_basic[c][j]&(1<<i))for(int dy=0;dy<scale;dy++)for(int dx=0;dx<scale;dx++)pixel(x+i*scale+dx,y+j*scale+dy,color);x+=8*scale;}}
public:
    bool stretch=false;
    int touch_x=400,touch_y=170;
    bool touching=false;
    ~Platform(){close();}
    bool open(bool display){
        if(display){
            fb_=::open("/dev/fb2",O_RDWR|O_CLOEXEC);fb_fix_screeninfo f{};
            if(fb_<0||ioctl(fb_,FBIOGET_VSCREENINFO,&initial_)||ioctl(fb_,FBIOGET_FSCREENINFO,&f)||initial_.xres!=340||initial_.yres!=800||initial_.bits_per_pixel!=32)return false;
            stride_=f.line_length;size_=f.smem_len;pages_=std::min(3,int(size_/(stride_*800)));if(!pages_)return false;
            auto *p=mmap(nullptr,size_,PROT_READ|PROT_WRITE,MAP_SHARED,fb_,0);if(p==MAP_FAILED)return false;mapped_=(uint8_t*)p;
            for(int n=0;n<pages_;n++){page_=mapped_+size_t(n)*stride_*800;blank(0);}
        }
        for(int i=0;i<3;i++){auto path="/dev/input/event"+std::to_string(i);keys_[i]=::open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);input_event e{};while(keys_[i]>=0&&read(keys_[i],&e,sizeof e)==sizeof e){}}
        return true;
    }
    void close(){
        if(mapped_){initial_.activate=FB_ACTIVATE_VBL;ioctl(fb_,FBIOPAN_DISPLAY,&initial_);munmap(mapped_,size_);mapped_=nullptr;}
        if(fb_>=0)::close(fb_);fb_=-1;for(auto &fd:keys_){if(fd>=0)::close(fd);fd=-1;}
    }
    template<class Callback> void poll(Callback callback){
        struct Event{input_event e;int device;};std::vector<Event> events;
        for(int i=0;i<3;i++){input_event e{};for(int n=0;keys_[i]>=0&&n<256&&read(keys_[i],&e,sizeof e)==sizeof e;n++)events.push_back({e,i});}
        std::stable_sort(events.begin(),events.end(),[](auto&a,auto&b){return a.e.time.tv_sec!=b.e.time.tv_sec?a.e.time.tv_sec<b.e.time.tv_sec:a.e.time.tv_usec<b.e.time.tv_usec;});
        for(auto &item:events){auto&e=item.e;int i=item.device;
            if(e.type==EV_SYN&&e.code==SYN_DROPPED){dropped_[i]=true;touching=false;callback(0,-1,0);continue;}
            if(dropped_[i]){if(e.type==EV_SYN&&e.code==SYN_REPORT)dropped_[i]=false;continue;}
            if(i==2){
                if(e.type==EV_ABS){if(e.code==ABS_X||e.code==ABS_MT_POSITION_X)touch_y=std::clamp(e.value,0,339);if(e.code==ABS_Y||e.code==ABS_MT_POSITION_Y)touch_x=799-std::clamp(e.value,0,799);if(e.code==ABS_MT_TRACKING_ID)touching=e.value>=0;}
                if(e.type==EV_KEY&&e.code==BTN_TOUCH)touching=e.value!=0;
            }else if(e.type==EV_KEY)callback(e.code,e.value,uint64_t(e.time.tv_sec)*1000+e.time.tv_usec/1000);
        }
    }
    bool inside()const{return touch_x>=(stretch?0:173)&&touch_x<(stretch?800:626);}
    int16_t pointer_x()const{return std::clamp((touch_x-(stretch?0:173))*65534/(stretch?799:452)-32767,-32767,32767);}
    int16_t pointer_y()const{return touch_y*65534/339-32767;}
    // Report actual guest image changes; padding bytes are not pixels.
    bool frame(const void *data,unsigned w,unsigned h,size_t pitch){
        if(!data||!w||!h||w>1024||h>1024||pitch<size_t(w)*4)return false;
        bool changed=w!=width_||h!=height_;
        if(!changed)for(unsigned y=0;y<h;y++)if(memcmp(last_.data()+size_t(y)*w,(const char*)data+y*pitch,w*4)){changed=true;break;}
        if(!changed)return false;
        width_=w;height_=h;last_.resize(size_t(w)*h);for(unsigned y=0;y<h;y++)memcpy(last_.data()+size_t(y)*w,(const char*)data+y*pitch,w*4);dirty_=true;return true;
    }
    void present(const std::string&hint={}){
        if(!mapped_||last_.empty()||(!dirty_&&stretch==last_stretch_&&hint==last_hint_))return;begin();blank(0);int vw=stretch?800:453,left=(800-vw)/2;unsigned yy[340];for(unsigned y=0;y<340;y++)yy[y]=(y*height_/340)*width_;
        for(int x=0;x<vw;x++){unsigned sx=x*width_/vw;auto *p=(uint32_t*)(page_+size_t(799-left-x)*stride_);for(int y=0;y<340;y++)p[y]=last_[yy[y]+sx]|0xff000000;}
        if(!hint.empty()){for(int x=0;x<800;x++)for(int y=322;y<340;y++)pixel(x,y,0x14232e);text(8,327,hint,0x8bddd2,1);}
        if(end()){dirty_=false;last_stretch_=stretch;last_hint_=hint;}
    }
    void menu(int selected,const std::vector<std::string>&items,const std::string&hint){
        if(!mapped_)return;dirty_=true;begin();blank(0x111c2b);text(30,20,"DOSBox / C1Max",0x8bddd2,3);
        for(size_t i=0;i<items.size();i++){text(30,75+34*i,int(i)==selected?">":" ",0xf0c778);text(60,75+34*i,items[i],int(i)==selected?0xf0c778:0xe4ecf1);}
        text(30,260,"W/S select   Enter confirm   Back resume",0xa4b5c2,2);
        text(30,294,"Camera tap: NAV / F1-F12 / Ctrl / Alt / symbols",0xa4b5c2,1);
        text(30,312,hint,0x8bddd2,1);end();
    }
};
}
