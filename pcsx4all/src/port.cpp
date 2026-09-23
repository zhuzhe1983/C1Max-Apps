// C1Max adapter for PCSX4all. GPL-2.0-or-later, like the emulator.
#include "port.h"
#include "r3000a.h"
#include "plugins.h"
#include "plugin_lib.h"
#include "gpu/gpu_unai/gpu.h"
#include "spu/spu_pcsxrearmed/spu_config.h"
#include "sio.h"
#include "../../launcher/src/font8x8.h"
#include <algorithm>
#include <csignal>
#include <string>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>

static unsigned short frame[320*240];
unsigned short *SCREEN=frame;
int c1_psx_mute=1;
static int fb=-1,keys[2]={-1,-1},stride=0,pages=0;
static uint8_t *memory=nullptr;
static size_t map_size=0;
static fb_var_screeninfo initial{};
static unsigned short pad=0xffff;
static volatile sig_atomic_t stop_requested=0;
static bool initialized=false,headless=false,library=false;
static unsigned smoke_frames=0,vsyncs=0;
static std::string data_dir,state_path;
static void stop(int){stop_requested=1;}
unsigned get_ticks(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return unsigned(t.tv_sec*1000000ULL+t.tv_nsec/1000);}
void wait_ticks(unsigned us){usleep(us);}
void video_clear(){memset(frame,0,sizeof frame);}
void port_printf(int x,int y,const char *s){
    for(;*s;s++,x+=8){unsigned ch=(unsigned char)*s;if(ch>=128)ch='?';
        for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(x+i>=0&&x+i<320&&y+j>=0&&y+j<240&&(font8x8_basic[ch][j]&(1<<i)))frame[(y+j)*320+x+i]=0xffff;}
}
void video_flip(){
    if(headless||!memory)return;
    fb_var_screeninfo v{};if(ioctl(fb,FBIOGET_VSCREENINFO,&v))return;
    int page=pages>1?(int(v.yoffset/800)+1)%pages:0;
    auto *dst=memory+size_t(page)*stride*800;
    for(int x=0;x<453;x++)for(int y=0;y<340;y++){
        uint16_t p=frame[(y*240/340)*320+x*320/453];uint32_t r=(p>>11)&31,g=(p>>5)&63,b=p&31;
        *(uint32_t*)(dst+size_t(799-(173+x))*stride+y*4)=0xff000000|((r*255/31)<<16)|((g*255/63)<<8)|(b*255/31);
    }
    __sync_synchronize();v.xoffset=0;v.yoffset=page*800;v.activate=FB_ACTIVATE_VBL;
    ioctl(fb,FBIOPAN_DISPLAY,&v);
}
static bool open_display(){
    fb=open("/dev/fb2",O_RDWR|O_CLOEXEC);fb_fix_screeninfo f{};
    if(fb<0||ioctl(fb,FBIOGET_VSCREENINFO,&initial)||ioctl(fb,FBIOGET_FSCREENINFO,&f)||initial.xres!=340||initial.yres!=800||initial.bits_per_pixel!=32)return false;
    stride=f.line_length;map_size=f.smem_len;pages=std::min(3,int(map_size/(stride*800)));if(!pages)return false;
    memory=(uint8_t*)mmap(0,map_size,PROT_READ|PROT_WRITE,MAP_SHARED,fb,0);if(memory==MAP_FAILED){memory=nullptr;return false;}
    memset(memory,0,map_size);
    for(int i=0;i<2;i++){std::string path="/dev/input/event"+std::to_string(i);keys[i]=open(path.c_str(),O_RDONLY|O_NONBLOCK|O_CLOEXEC);input_event e;while(read(keys[i],&e,sizeof e)==sizeof e){}}
    return true;
}
static void cleanup(){
    if(initialized){sioSyncMcds();ReleasePlugins();psxShutdown();initialized=false;}
    if(memory){initial.activate=FB_ACTIVATE_VBL;ioctl(fb,FBIOPAN_DISPLAY,&initial);munmap(memory,map_size);memory=nullptr;}
    if(fb>=0)close(fb);for(int fd:keys)if(fd>=0)close(fd);
}
static int fail(const char *message,int code=1){
    fprintf(stderr,"%s\n",message);cleanup();fflush(stdout);
    const char *root=getenv("C1_APPS_ROOT");
    if(!headless&&root){setenv("C1_PSX_ERROR",message,1);std::string path=std::string(root)+"/pcsx4all/c1max-pcsx4all";execl(path.c_str(),path.c_str(),(char*)nullptr);}
    return code;
}
static void finish(){
    struct rusage ru;getrusage(RUSAGE_SELF,&ru);printf("C1MAX_PCSX_DONE vsyncs=%u rss_peak_kb=%ld\n",vsyncs,ru.ru_maxrss);
    cleanup();
    fflush(stdout);
    if(library){const char *root=getenv("C1_APPS_ROOT");std::string path=std::string(root?root:"/storage/apps/current")+"/pcsx4all/c1max-pcsx4all";execl(path.c_str(),path.c_str(),(char*)nullptr);}
    exit(0);
}
static void menu(){
    pl_pause();sioSyncMcds();pad=0xffff;int selected=0;bool done=false,dirty=true;std::string message;
    while(!done&&!stop_requested){
        if(dirty){dirty=false;
        video_clear();port_printf(56,28,"PCSX4all / C1Max");
        const char *items[]={"Resume","Save state (slot 1)","Load state (slot 1)","Game library"};
        for(int i=0;i<4;i++){port_printf(28,66+i*28,i==selected?">":" ");port_printf(48,66+i*28,items[i]);}
        port_printf(24,196,"W/S move  Enter select");port_printf(24,210,"Back resume  Power home");port_printf(24,228,message.c_str());video_flip();}
        for(int fd:keys){input_event e;while(read(fd,&e,sizeof e)==sizeof e){if(e.type!=EV_KEY||e.value!=1)continue;
            dirty=true;
            if(e.code==KEY_POWER){stop_requested=1;break;}
            if(e.code==14){done=true;break;}
            if(e.code==KEY_W)selected=(selected+3)%4;if(e.code==KEY_S)selected=(selected+1)%4;
            if(e.code==KEY_ENTER||e.code==KEY_J){
                if(selected==0)done=true;
                if(selected==1){const std::string tmp=state_path+".tmp";if(SaveState(tmp.c_str())==0&&rename(tmp.c_str(),state_path.c_str())==0)message="State saved";else message="Save failed";}
                if(selected==2){if(access(state_path.c_str(),R_OK)!=0)message="No saved state";else if(LoadState(state_path.c_str())==0){message="State loaded";done=true;}else message="Load failed";}
                if(selected==3){library=true;stop_requested=1;}
            }
        }}usleep(20000);
    }
    video_clear();pad=0xffff;pl_resume();
}
void pad_update(){
    ++vsyncs;if(stop_requested||(smoke_frames&&vsyncs>=smoke_frames))finish();
    for(int fd:keys){input_event e;while(read(fd,&e,sizeof e)==sizeof e){
        if(e.type==EV_SYN&&e.code==SYN_DROPPED){pad=0xffff;continue;}
        if(e.type!=EV_KEY)continue;int bit=-1;
        switch(e.code){case KEY_W:bit=4;break;case KEY_D:bit=5;break;case KEY_S:bit=6;break;case KEY_A:bit=7;break;
        case KEY_Q:bit=10;break;case KEY_E:bit=11;break;case KEY_Z:bit=8;break;case KEY_C:bit=9;break;
        case KEY_I:bit=12;break;case KEY_K:bit=13;break;case KEY_J:bit=14;break;case KEY_U:bit=15;break;
        case KEY_ENTER:bit=3;break;case KEY_SPACE:bit=0;break;case KEY_POWER:if(e.value==1)stop_requested=1;break;
        case 14:if(e.value==1)menu();break;default:break;}
        if(bit>=0){if(e.value)pad&=~(1<<bit);else pad|=1<<bit;}
    }}
    if(stop_requested)finish();
}
unsigned short pad_read(int n){return n==0?pad:0xffff;}
static void copy_path(char *dst,size_t n,const std::string &s){if(s.size()>=n){fprintf(stderr,"Path too long\n");exit(2);}memcpy(dst,s.c_str(),s.size()+1);}
int main(int argc,char **argv){
    std::string image,bios;bool interpreter=false;
    for(int i=1;i<argc;i++){std::string a=argv[i];
        if(a=="--image"&&i+1<argc)image=argv[++i];else if(a=="--bios"&&i+1<argc)bios=argv[++i];
        else if(a=="--interpreter")interpreter=true;else if(a=="--mute")c1_psx_mute=1;else if(a=="--audio")c1_psx_mute=0;
        else if(a=="--headless")headless=true;else if(a=="--smoke-frames"&&i+1<argc){smoke_frames=atoi(argv[++i]);if(!smoke_frames||smoke_frames>36000)return 2;}
        else{fprintf(stderr,"Usage: %s --image file [--bios file] [--interpreter] [--mute|--audio] [--headless --smoke-frames N]\n",argv[0]);return 2;}}
    if(image.empty()||image.size()>240||access(image.c_str(),R_OK)){return fail("Game image unavailable",2);}
    signal(SIGTERM,stop);signal(SIGINT,stop);umask(0077);
    const char *d=getenv("C1_APPS_DATA");std::string base=d?d:"/storage/apps/data";mkdir(base.c_str(),0700);data_dir=base+"/pcsx4all";mkdir(data_dir.c_str(),0700);
    for(auto folder:{"/memcards","/states","/bios","/patches"})mkdir((data_dir+folder).c_str(),0700);
    copy_path(Config.Mcd1,sizeof Config.Mcd1,data_dir+"/memcards/slot1.mcr");copy_path(Config.Mcd2,sizeof Config.Mcd2,data_dir+"/memcards/slot2.mcr");
    copy_path(Config.PatchesDir,sizeof Config.PatchesDir,data_dir+"/patches");copy_path(Config.BiosDir,sizeof Config.BiosDir,data_dir+"/bios");
    Config.HLE=1;
    if(!bios.empty()){struct stat st{};if(stat(bios.c_str(),&st)||st.st_size!=524288){return fail("BIOS must be 512 KiB",2);}
        auto slash=bios.find_last_of('/');copy_path(Config.BiosDir,sizeof Config.BiosDir,slash==std::string::npos?".":bios.substr(0,slash));copy_path(Config.Bios,sizeof Config.Bios,slash==std::string::npos?bios:bios.substr(slash+1));Config.HLE=0;}
    Config.Cpu=interpreter?1:0;Config.PsxAuto=1;Config.FrameLimit=1;Config.FrameSkip=FRAMESKIP_AUTO;Config.SpuUpdateFreq=2;Config.ForcedXAUpdates=1;
    spu_config.iHaveConfiguration=1;spu_config.iVolume=c1_psx_mute?0:1024;spu_config.iUseInterpolation=0;spu_config.iTempo=1;spu_config.iUseFixedUpdates=1;
    gpu_unai_config_ext.lighting=1;gpu_unai_config_ext.blending=1;gpu_unai_config_ext.fast_lighting=1;gpu_unai_config_ext.dithering=0;
    FILE *fp=fopen(image.c_str(),"rb");char magic[8]={};if(!fp)return fail("Game image unavailable",2);fread(magic,1,8,fp);fclose(fp);bool executable=!memcmp(magic,"PS-X EXE",8);
    if(!executable)SetIsoFile(image.c_str());
    if(!headless&&!open_display()){return fail("Framebuffer unavailable");}
    if(psxInit()<0)return fail("Emulator memory initialization failed");initialized=true;
    if(LoadPlugins()<0)return fail("Emulator plugin initialization failed");pl_init();psxReset();
    if((executable?Load(image.c_str()):(CheckCdrom()<0?-1:LoadCdrom()))<0){return fail("Game could not be loaded");}
    uint64_t hash=1469598103934665603ULL;for(unsigned char c:image){hash^=c;hash*=1099511628211ULL;}char id[32];snprintf(id,sizeof id,"%016llx",(unsigned long long)hash);
    state_path=data_dir+"/states/"+id+".0.sav";
    printf("C1MAX_PCSX_START cpu=%s bios=%s muted=%d\n",interpreter?"interpreter":"mips-dynarec",Config.HLE?"HLE":"external",c1_psx_mute);fflush(stdout);
    psxCpu->Execute();cleanup();return 0;
}
