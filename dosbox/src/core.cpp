// GPL-2.0-or-later. Minimal libretro host; no RetroArch or desktop required.
#include "libretro.h"
#include "input.hpp"
#include "platform.hpp"
#include "tinyalsa/asoundlib.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <filesystem>
#include <fstream>
#include <csignal>
#include <sys/stat.h>
#include <sys/resource.h>
namespace fs=std::filesystem;
namespace {
std::map<std::string,std::string> options;
retro_keyboard_event_t keyboard_callback=nullptr;
volatile sig_atomic_t stop_requested=0;
bool shutdown_requested=false,menu_requested=false,library_requested=false,headless=false,audio_enabled=false;
unsigned frames=0,frame_limit=0,cycles=2750,memsize=8;
uint64_t hash=0,last_present=0,hint_until=0;
double fps=70.086;
std::string directory,root;
dos::Platform platform;
dos::Input keys;
pcm *audio_device=nullptr;
void stop(int){stop_requested=1;}
void logger(enum retro_log_level level,const char* format,...){if(level<RETRO_LOG_WARN)return;va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);}
bool environment(unsigned cmd,void *data){
    switch(cmd){
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:((retro_log_callback*)data)->log=logger;return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:*(const char**)data=directory.c_str();return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:*(unsigned*)data=2;return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:for(auto *d=((retro_core_options_v2*)data)->definitions;d&&d->key;d++)if(!options.count(d->key))options[d->key]=d->default_value?d->default_value:"";return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:{auto *v=(retro_variable*)data;auto i=options.find(v->key);v->value=i==options.end()?nullptr:i->second.c_str();return v->value!=nullptr;}
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)data=false;return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:return *(retro_pixel_format*)data==RETRO_PIXEL_FORMAT_XRGB8888;
    case RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK:keyboard_callback=((retro_keyboard_callback*)data)->callback;return true;
    case RETRO_ENVIRONMENT_SHUTDOWN:shutdown_requested=true;return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:fprintf(stderr,"DOS: %s\n",((retro_message_ext*)data)->msg);return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:{auto &av=*(retro_system_av_info*)data;if(av.timing.fps>1&&av.timing.fps<200)fps=av.timing.fps;return true;}
    case RETRO_ENVIRONMENT_SET_GEOMETRY:return true;
    default:return false;
    }
}
void video(const void *data,unsigned width,unsigned height,size_t pitch){
    if(!data||!width||!height||width>1024||height>1024||pitch<size_t(width)*4)return;
    ++frames;auto *p=(const uint8_t*)data;uint64_t h=1469598103934665603ULL;
    for(unsigned y=0;y<height;y+=8)for(unsigned x=0;x<width;x+=8){uint32_t pixel;memcpy(&pixel,p+y*pitch+x*4,4);h^=pixel;h*=1099511628211ULL;}hash=h;
    if(frames==1){printf("DOS_VIDEO %ux%u pitch=%zu\n",width,height,pitch);fflush(stdout);}
    auto now=dos::micros();
    if(!headless&&now-last_present>=33333){platform.frame(data,width,height,pitch);platform.present(now<hint_until?keys.hint():"");last_present=now;}
}
void sample(int16_t,int16_t){}
size_t batch(const int16_t *data,size_t n){
    if(audio_device&&pcm_writei(audio_device,data,n)<0){fprintf(stderr,"DOS audio failed; continuing silently\n");pcm_close(audio_device);audio_device=nullptr;}
    return n;
}
void poll(){
    platform.poll([](unsigned code,int value,uint64_t ms){
        if(value<0){keys.clear();return;}
        if(menu_requested)return;
        auto action=keys.event(code,value,ms);
        if(action==dos::Input::Home)stop_requested=1;
        if(action==dos::Input::Menu){menu_requested=true;platform.touching=false;}
        if(action==dos::Input::Hint)hint_until=dos::micros()+3000000;
    });
}
int16_t input(unsigned,unsigned device,unsigned,unsigned id){
    if(device==RETRO_DEVICE_KEYBOARD)return keys.down(id);
    if(device==RETRO_DEVICE_POINTER){if(id==RETRO_DEVICE_ID_POINTER_X)return platform.pointer_x();if(id==RETRO_DEVICE_ID_POINTER_Y)return platform.pointer_y();if(id==RETRO_DEVICE_ID_POINTER_PRESSED||id==RETRO_DEVICE_ID_POINTER_COUNT)return platform.touching&&platform.inside();}
    if(device==RETRO_DEVICE_MOUSE&&id==RETRO_DEVICE_ID_MOUSE_LEFT)return platform.touching&&platform.inside();
    return 0;
}
void menu(){
    keys.clear();platform.touching=false;int selected=0;bool dirty=true;
    while(menu_requested&&!stop_requested){
        if(dirty){platform.menu(selected,{"Resume",keys.game()?"Keyboard: GAME":"Keyboard: TEXT",platform.stretch?"Display: fill width":"Display: 4:3","Game library"},keys.hint());dirty=false;}
        platform.poll([&](unsigned code,int value,uint64_t){
            if(value!=1)return;
            if(code==116){stop_requested=1;return;}if(code==14){menu_requested=false;return;}
            if(code==17){selected=(selected+3)%4;dirty=true;}if(code==31){selected=(selected+1)%4;dirty=true;}
            if(code==28){if(selected==0)menu_requested=false;if(selected==1){keys.set_game(!keys.game());dirty=true;}if(selected==2){platform.stretch=!platform.stretch;dirty=true;}if(selected==3){library_requested=true;stop_requested=1;}}
        });usleep(12000);
    }
    keys.clear();platform.touching=false;platform.present();hint_until=dos::micros()+2500000;
}
int finish(int code,const char *error=nullptr){
    if(audio_device){pcm_close(audio_device);audio_device=nullptr;}platform.close();
    if(error)fprintf(stderr,"DOS: %s\n",error);
    if(!headless&&(library_requested||shutdown_requested||error)){
        if(error)setenv("C1_DOS_ERROR",error,1);
        auto path=root+"/dosbox/c1max-dosbox";execl(path.c_str(),path.c_str(),(char*)nullptr);
    }
    return code;
}
}
int main(int argc,char **argv){
    root=getenv("C1_APPS_ROOT")?getenv("C1_APPS_ROOT"):"/storage/apps/current";
    directory=std::string(getenv("C1_APPS_DATA")?getenv("C1_APPS_DATA"):"/storage/apps/data")+"/dosbox";
    std::string file;bool shell=false,game=false;
    for(int i=1;i<argc;i++){std::string a=argv[i];
        if(a=="--file"&&i+1<argc)file=argv[++i];else if(a=="--shell")shell=true;else if(a=="--game")game=true;
        else if(a=="--stretch")platform.stretch=true;else if(a=="--audio")audio_enabled=true;else if(a=="--mute")audio_enabled=false;
        else if(a=="--headless")headless=true;else if(a=="--frames"&&i+1<argc)frame_limit=atoi(argv[++i]);
        else if(a=="--cycles"&&i+1<argc)cycles=atoi(argv[++i]);else if(a=="--memory"&&i+1<argc)memsize=atoi(argv[++i]);else return 2;
    }
    if((file.empty()&&!shell)||(!file.empty()&&shell)||frame_limit>36000||(memsize!=8&&memsize!=16)||(cycles!=2750&&cycles!=4720&&cycles!=7800))return 2;
    signal(SIGTERM,stop);signal(SIGINT,stop);umask(0077);
    std::error_code ec;fs::create_directories(directory+"/games",ec);if(ec)return finish(1,"Data directory unavailable");
    std::string mount=directory+"/games",command;bool batch_file=false;
    if(!shell){
        auto path=fs::absolute(file,ec);if(ec||!fs::is_regular_file(path,ec))return finish(2,"Program file unavailable");
        mount=path.parent_path().string();command=path.filename().string();auto dot=command.find('.');
        if(dot==std::string::npos||dot==0||dot>8||command.size()-dot!=4||command.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.")!=std::string::npos)return finish(2,"Use an ASCII DOS 8.3 program name, e.g. GAME.EXE");
        auto ext=command.substr(dot);std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::toupper(c);});if(ext!=".EXE"&&ext!=".COM"&&ext!=".BAT")return finish(2,"Choose an EXE, COM or BAT program");batch_file=ext==".BAT";
    }
    if(mount.find_first_of("\"\r\n")!=std::string::npos)return finish(2,"Unsupported folder name");
    auto conf=directory+"/session.conf";
    {std::ofstream out(conf,std::ios::trunc);out<<"[autoexec]\n@echo off\nmount c \""<<mount<<"\"\nc:\n";if(!shell)out<<(batch_file?"call ":"")<<command<<"\nexit\n";out.flush();if(!out)return finish(1,"Cannot write DOS configuration");}
    options={{"dosbox_pure_memory_size",std::to_string(memsize)},{"dosbox_pure_cpu_core","normal"},{"dosbox_pure_cycles",std::to_string(cycles)},{"dosbox_pure_savestate","disabled"},{"dosbox_pure_midi","disabled"},{"dosbox_pure_machine","vga"},{"dosbox_pure_voodoo","off"},{"dosbox_pure_voodoo_perf","0"},{"dosbox_pure_on_screen_keyboard","false"},{"dosbox_pure_auto_mapping","false"},{"dosbox_pure_mouse_input","direct"},{"dosbox_pure_menu_time","0"},{"dosbox_pure_audiorate","44100"}};
    if(!platform.open(!headless))return finish(1,"Framebuffer unavailable");
    keys.set_game(game);keys.send=[](bool down,unsigned key){if(keyboard_callback)keyboard_callback(down,key,0,0);};
    retro_set_environment(environment);retro_set_video_refresh(video);retro_set_audio_sample(sample);retro_set_audio_sample_batch(batch);retro_set_input_poll(poll);retro_set_input_state(input);retro_init();
    retro_game_info info{conf.c_str(),nullptr,0,nullptr};if(!retro_load_game(&info)){retro_deinit();return finish(1,"DOS core could not load the program");}
    retro_system_av_info av{};retro_get_system_av_info(&av);if(av.timing.fps>1&&av.timing.fps<200)fps=av.timing.fps;
    if(audio_enabled){pcm_config c{};c.channels=2;c.rate=av.timing.sample_rate;c.format=PCM_FORMAT_S16_LE;c.period_size=1024;c.period_count=4;audio_device=pcm_open(0,0,PCM_OUT,&c);if(!audio_device||!pcm_is_ready(audio_device)){if(audio_device)pcm_close(audio_device);audio_device=nullptr;fprintf(stderr,"DOS audio unavailable; muted\n");}}
    printf("DOS_READY fps=%.2f audio=%.0f muted=%d memory=%u cycles=%u cpu=normal\n",fps,av.timing.sample_rate,!audio_device,memsize,cycles);fflush(stdout);
    auto started=dos::micros(),deadline=started;hint_until=started+3000000;unsigned runs=0;
    while(!stop_requested&&!shutdown_requested&&(!frame_limit||runs<frame_limit)){
        retro_run();++runs;if(menu_requested){menu();deadline=dos::micros();}
        deadline+=uint64_t(1000000/fps);auto now=dos::micros();if(now<deadline)usleep(deadline-now);else if(now-deadline>100000)deadline=now;
    }
    keys.clear();retro_unload_game();retro_deinit();
    rusage usage{};getrusage(RUSAGE_SELF,&usage);printf("DOS_DONE runs=%u frames=%u hash=%llx peak_kB=%ld elapsed_ms=%llu\n",runs,frames,(unsigned long long)hash,usage.ru_maxrss,(unsigned long long)((dos::micros()-started)/1000));fflush(stdout);
    // A short BAT/COM may finish before producing its first video frame.
    return finish(frames||shutdown_requested?0:1);
}
