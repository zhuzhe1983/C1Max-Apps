// GPL-2.0-or-later. A single framebuffer owner for the DOS frontend.
#pragma once
#include "guide.hpp"
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
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
    std::vector<uint32_t> last_,guides_[9],notice_,notice_large_;
    unsigned width_=0,height_=0;
    bool dirty_=true,last_stretch_=false,last_game_=true,last_caps_=false,last_power_notice_=false;
    int last_prefix_=0;
    clockid_t event_clock_=CLOCK_REALTIME;
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
            const char *root=getenv("C1_APPS_ROOT");std::string path=std::string(root?root:"/storage/apps/current")+"/shared/NotoSansSC-Regular.ttf";
            bool font=typeface_open(path.c_str());std::vector<uint32_t> panel;
            for(int mode=0;mode<9;mode++){
                dosguide::panel(panel,font,mode);guides_[mode].resize(dosguide::Width*340);
                for(int x=0;x<dosguide::Width;x++)for(int y=0;y<340;y++)guides_[mode][x*340+y]=panel[y*dosguide::Width+x];
            }
            notice_.assign(98*44,0xff18252f);
            for(int x=0;x<98;x++)for(int y=0;y<44;y++)if(x==0||y==0||x==97||y==43)notice_[y*98+x]=0xff526a76;
            notice_large_.assign(260*48,0xff18252f);
            if(font){
                const char *lines[]={"长按五秒","电源键退出"};
                for(int n=0;n<2;n++){int w=typeface_width(lines[n],13);typeface_draw(notice_.data(),98,44,(98-w)/2,3+n*19,lines[n],0xffd8eceb,13);}
                const char *message="长按五秒电源键退出";int w=typeface_width(message,16);typeface_draw(notice_large_.data(),260,48,(260-w)/2,8,message,0xffd8eceb,16);
            }
            typeface_close();
            for(int n=0;n<pages_;n++){page_=mapped_+size_t(n)*stride_*800;blank(0);}
        }
        for(int i=0;i<3;i++){auto path="/dev/input/event"+std::to_string(i);keys_[i]=::open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);}
        bool monotonic=true;clockid_t wanted=CLOCK_MONOTONIC;
#ifdef EVIOCSCLOCKID
        for(auto fd:keys_)if(fd>=0&&ioctl(fd,EVIOCSCLOCKID,&wanted)<0)monotonic=false;
        if(!monotonic){wanted=CLOCK_REALTIME;for(auto fd:keys_)if(fd>=0)ioctl(fd,EVIOCSCLOCKID,&wanted);}
#else
        monotonic=false;
#endif
        event_clock_=monotonic?CLOCK_MONOTONIC:CLOCK_REALTIME;
        input_event e{};for(auto fd:keys_)while(fd>=0&&read(fd,&e,sizeof e)==sizeof e){}
        return true;
    }
    uint64_t event_clock_ms()const{timespec t{};clock_gettime(event_clock_,&t);return uint64_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
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
    int viewport_width()const{return stretch?604:453;}
    int viewport_left()const{return (800-viewport_width())/2;}
    bool inside()const{return touch_x>=viewport_left()&&touch_x<viewport_left()+viewport_width();}
    int16_t pointer_x()const{return std::clamp((touch_x-viewport_left())*65534/(viewport_width()-1)-32767,-32767,32767);}
    int16_t pointer_y()const{return touch_y*65534/339-32767;}
    // Report actual guest image changes; padding bytes are not pixels.
    bool frame(const void *data,unsigned w,unsigned h,size_t pitch){
        if(!data||!w||!h||w>1024||h>1024||pitch<size_t(w)*4)return false;
        bool changed=w!=width_||h!=height_;
        if(!changed)for(unsigned y=0;y<h;y++)if(memcmp(last_.data()+size_t(y)*w,(const char*)data+y*pitch,w*4)){changed=true;break;}
        if(!changed)return false;
        width_=w;height_=h;last_.resize(size_t(w)*h);for(unsigned y=0;y<h;y++)memcpy(last_.data()+size_t(y)*w,(const char*)data+y*pitch,w*4);dirty_=true;return true;
    }
    void present(const std::string&hint={},bool game=true,int prefix=0,bool caps=false,bool power_notice=false){
        if(!mapped_||last_.empty()||(!dirty_&&stretch==last_stretch_&&hint==last_hint_&&game==last_game_&&prefix==last_prefix_&&caps==last_caps_&&power_notice==last_power_notice_))return;begin();blank(0);int vw=viewport_width(),left=viewport_left();unsigned yy[340];for(unsigned y=0;y<340;y++)yy[y]=(y*height_/340)*width_;
        for(int x=0;x<vw;x++){unsigned sx=x*width_/vw;auto *p=(uint32_t*)(page_+size_t(799-left-x)*stride_);for(int y=0;y<340;y++)p[y]=last_[yy[y]+sx]|0xff000000;}
        for(int side=0;side<2;side++){
            int edge=side?left+vw:0,available=side?800-edge:left;
            for(int x=edge;x<edge+available;x++){auto *col=(uint32_t*)(page_+size_t(799-x)*stride_);std::fill(col,col+340,0xff0b121a);}
            int panel_left=edge+(available-dosguide::Width)/2;
            int mode=side?2:prefix>=1&&prefix<=5?prefix+2:game?0:caps?8:1;
            for(int x=0;x<dosguide::Width;x++)memcpy(page_+size_t(799-panel_left-x)*stride_,guides_[mode].data()+x*340,340*4);
        }
        if(power_notice){
            const int panel_left=(left-dosguide::Width)/2,banner_w=98,banner_h=44,bx=panel_left,by=8;
            for(int y=0;y<banner_h;y++)for(int x=0;x<banner_w;x++)pixel(bx+x,by+y,notice_[y*banner_w+x]);
        }
        if(end()){dirty_=false;last_stretch_=stretch;last_hint_=hint;last_game_=game;last_prefix_=prefix;last_caps_=caps;last_power_notice_=power_notice;}
    }
    void menu(int selected,const std::vector<std::string>&items,const std::string&hint,bool power_notice=false){
        if(!mapped_)return;dirty_=true;begin();blank(0x111c2b);text(30,20,"DOSBox / C1Max",0x8bddd2,3);
        for(size_t i=0;i<items.size();i++){text(30,75+34*i,int(i)==selected?">":" ",0xf0c778);text(60,75+34*i,items[i],int(i)==selected?0xf0c778:0xe4ecf1);}
        text(30,260,"W/S select   Enter confirm   Back resume",0xa4b5c2,2);
        text(30,294,"Camera tap: NAV / F1-F12 / Ctrl / Alt / symbols",0xa4b5c2,1);
        text(30,312,hint,0x8bddd2,1);
        if(power_notice)for(int y=0;y<48;y++)for(int x=0;x<260;x++)pixel(270+x,175+y,notice_large_[y*260+x]);
        end();
    }
};
}
