#include "display.hpp"
#include "keyboard.hpp"
#include "video_layout.hpp"
#include <vector>
#include <cstring>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <cstdio>
#include <initializer_list>
#include <new>
namespace screen {
bool quit=false,playing=false,tap=false;
static int fb=-1,touch=-1,key=-1,stride=0,frames=0;static size_t length=0;static uint8_t*memory=nullptr;
static uint32_t buffer[800*24];
static fb_var_screeninfo video_saved{};
static bool video_has_saved=false,video_active=false,video_visible=true,video_dirty=false;
static bool video_pan_error=false,video_width_fill=true;
static uint32_t *video_ui=nullptr,video_last_refresh=0;
static std::vector<uint32_t> video_rgb;
static VideoLayout video_layout;
static int video_width=0,video_height=0,video_aspect_n=1,video_aspect_d=1;
static constexpr int native_width=340,native_height=800;
uint32_t take_key(){return keyboard::take();}
bool caps_lock(){return keyboard::caps_lock();}

bool video_begin(){
    if(video_active)return true;
    if(fb<0||!memory||frames<2)return false;
    if(ioctl(fb,FBIOGET_VSCREENINFO,&video_saved)!=0)return false;
    if(video_saved.xres!=native_width||video_saved.yres!=native_height||video_saved.bits_per_pixel!=32||video_saved.yres_virtual<1600){
        std::fputs("[display] Complete-frame video needs two framebuffer pages\n",stderr);return false;
    }
    video_ui=new(std::nothrow) uint32_t[native_width*native_height];
    if(!video_ui)return false;
    std::fill_n(video_ui,native_width*native_height,0xff111827);
    video_has_saved=video_active=video_visible=video_dirty=true;
    video_width=video_height=0;video_rgb.clear();video_layout.configure(2,2,1,1,true);
    video_last_refresh=tick()-40;video_refresh();
    if(lv_screen_active())lv_obj_invalidate(lv_screen_active());return true;
}
void video_frame(const uint32_t *rgb,int width,int height,int aspect_n,int aspect_d){
    if(!video_active||width<2||height<2||width>512||height>288||aspect_n<=0||aspect_d<=0)return;
    video_rgb.assign(rgb,rgb+size_t(width)*height);
    if(video_width!=width||video_height!=height||video_aspect_n!=aspect_n||video_aspect_d!=aspect_d){
        video_width=width;video_height=height;video_aspect_n=aspect_n;video_aspect_d=aspect_d;
        video_layout.configure(width,height,aspect_n,aspect_d,video_width_fill);
    }
    video_dirty=true;
}
void video_fit(bool width_fill){
    video_width_fill=width_fill;
    if(video_width>0)video_layout.configure(video_width,video_height,video_aspect_n,video_aspect_d,width_fill);
    video_dirty=true;
}
void video_controls(bool visible){
    video_visible=visible;video_dirty=true;
    if(video_active&&visible&&lv_screen_active())lv_obj_invalidate(lv_screen_active());
}
void video_refresh(bool){
    if(!video_active||!memory||!video_ui||!video_dirty)return;
    uint32_t now=tick();if(uint32_t(now-video_last_refresh)<40)return;
    fb_var_screeninfo v{};if(ioctl(fb,FBIOGET_VSCREENINFO,&v)!=0)return;
    // Only this process opens the framebuffer during playback. A decoder
    // delivers complete YUV frames through a FIFO and never changes scanout.
    if(v.xres!=340||v.yres!=800||v.bits_per_pixel!=32||v.yres_virtual!=video_saved.yres_virtual)return;
    int page=-1;
    for(int candidate=0;candidate<frames&&candidate*800+800<=int(v.yres_virtual);candidate++){
        uint64_t start=uint64_t(candidate)*800,end=start+800;
        if(end<=v.yoffset||start>=uint64_t(v.yoffset)+800){page=candidate;break;}
    }
    if(page<0)return;
    video_layout.compose(memory+size_t(page)*stride*800,stride,video_rgb.empty()?nullptr:video_rgb.data(),video_width,video_ui,video_visible);
    __sync_synchronize();v.xoffset=0;v.yoffset=page*800;v.activate=FB_ACTIVATE_VBL;
    if(ioctl(fb,FBIOPAN_DISPLAY,&v)!=0){if(!video_pan_error)perror("[display] Present complete frame");video_pan_error=true;return;}
    video_pan_error=false;video_dirty=false;video_last_refresh=now;
}
void video_end(){
    video_active=false;
    if(video_has_saved&&fb>=0){auto v=video_saved;v.activate=FB_ACTIVATE_VBL;if(ioctl(fb,FBIOPAN_DISPLAY,&v)!=0)perror("[display] Restore framebuffer page");}
    video_has_saved=false;video_dirty=false;video_rgb.clear();
    delete[] video_ui;video_ui=nullptr;
}
static int input_x=0,input_y=0,input_down=0;
uint32_t tick(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return uint32_t(t.tv_sec*1000+t.tv_nsec/1000000);}
static void flush(lv_display_t*d,const lv_area_t*a,uint8_t*p){
    if(memory){auto src=reinterpret_cast<uint32_t*>(p);for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++){
        auto color=*src++ | 0xff000000;
        if(x<0||x>=native_height||y<0||y>=native_width)continue;
        if(video_active&&video_ui)video_ui[size_t(799-x)*native_width+y]=color;
        else if(!playing)for(int f=0;f<frames;f++)*reinterpret_cast<uint32_t*>(memory+f*stride*800+(799-x)*stride+y*4)=color;
    }}
    if(video_active)video_dirty=true;
    lv_display_flush_ready(d);
}
static void input(lv_indev_t*,lv_indev_data_t*d){
    int &x=input_x,&y=input_y,&down=input_down;input_event e;
    // Return one SYN frame per LVGL read so a fast tap is not lost by draining it.
    while(read(touch,&e,sizeof(e))==sizeof(e)){
        if(e.type==EV_ABS){if(e.code==ABS_X)x=e.value;else if(e.code==ABS_Y)y=e.value;}
        if(e.type==EV_KEY&&e.code==BTN_TOUCH)down=e.value;
        if(e.type==EV_SYN&&e.code==SYN_REPORT){d->continue_reading=true;break;}
    }

    d->point.x=LV_CLAMP(0,799-y,799);d->point.y=LV_CLAMP(0,x,339);d->state=down?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
}
bool open(){
    fb=::open("/dev/fb2",O_RDWR|O_CLOEXEC);fb_var_screeninfo v{};fb_fix_screeninfo f{};
    if(fb<0||ioctl(fb,FBIOGET_VSCREENINFO,&v)||ioctl(fb,FBIOGET_FSCREENINFO,&f)||v.xres!=340||v.yres!=800||v.bits_per_pixel!=32||f.line_length<1360){perror("C1Max framebuffer");return false;}
    stride=f.line_length;length=f.smem_len;frames=length/(stride*800);if(frames<1)return false;if(frames>3)frames=3;
    memory=(uint8_t*)mmap(nullptr,length,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);if(memory==MAP_FAILED){memory=nullptr;return false;}
    touch=::open("/dev/input/event2",O_RDONLY|O_NONBLOCK|O_CLOEXEC);key=::open("/dev/input/event1",O_RDONLY|O_NONBLOCK|O_CLOEXEC);
    input_event stale;for(int fd:{touch,key})while(fd>=0&&read(fd,&stale,sizeof stale)==sizeof stale){}
    input_absinfo axis{};if(ioctl(touch,EVIOCGABS(ABS_X),&axis)==0)input_x=axis.value;
    if(ioctl(touch,EVIOCGABS(ABS_Y),&axis)==0)input_y=axis.value;
    keyboard::open();
    lv_init();lv_tick_set_cb(tick);auto disp=lv_display_create(800,340);lv_display_set_color_format(disp,LV_COLOR_FORMAT_ARGB8888);lv_display_set_buffers(disp,buffer,nullptr,sizeof(buffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(disp,flush);
    auto indev=lv_indev_create();lv_indev_set_type(indev,LV_INDEV_TYPE_POINTER);lv_indev_set_read_cb(indev,input);return true;
}
void close(){
    video_end();keyboard::close();
    if(memory)munmap(memory,length);
    if(fb>=0)::close(fb);if(touch>=0)::close(touch);if(key>=0)::close(key);
    memory=nullptr;fb=touch=key=-1;length=0;frames=0;
}
}
