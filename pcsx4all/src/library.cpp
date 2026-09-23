#include "display.hpp"
#include "net.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <algorithm>
#include <csignal>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
namespace {
volatile sig_atomic_t stop=0;
void signal_stop(int){stop=1;}
std::vector<std::string> games;
size_t selected=0;
bool muted=true,interpreter=false,chinese=true;
std::string folder,bios,message;
lv_font_t *font;
lv_obj_t *list=nullptr,*info=nullptr;
const char *tr(const char *cn,const char *en){return chinese?cn:en;}
void render();
void settings(){try{c1::save_private(c1::data()+"/pcsx4all/settings.json",Json{{"muted",muted},{"interpreter",interpreter}}.dump());}catch(...){message=tr("设置保存失败","Could not save settings");}}
void start(){
    if(games.empty())return;settings();
    std::string executable=c1::root()+"/pcsx4all/c1max-psx-core";
    if(access(executable.c_str(),X_OK)){message=tr("模拟器文件缺失，请重新安装","Emulator missing; reinstall app");render();return;}
    std::vector<std::string> args={executable,"--image",games[selected],muted?"--mute":"--audio"};
    if(!bios.empty()){args.push_back("--bios");args.push_back(bios);}if(interpreter)args.push_back("--interpreter");
    std::vector<char*> av;for(auto &a:args)av.push_back(a.data());av.push_back(nullptr);
    screen::close();execv(executable.c_str(),av.data());_exit(1);
}
void scan(){
    games.clear();std::error_code ec;fs::recursive_directory_iterator it(folder,fs::directory_options::skip_permission_denied,ec),end;
    while(!ec&&it!=end&&games.size()<200){
        if(it.depth()>=3)it.disable_recursion_pending();
        if(it->is_regular_file(ec)){auto ext=it->path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});
            if(ext==".cue"||ext==".bin"||ext==".img"||ext==".iso"||ext==".pbp"||ext==".exe")games.push_back(it->path().string());}
        it.increment(ec);
    }
    std::sort(games.begin(),games.end());selected=std::min(selected,games.empty()?size_t(0):games.size()-1);
    bios.clear();std::string b=c1::data()+"/pcsx4all/bios/scph1001.bin";struct stat st{};if(!stat(b.c_str(),&st)&&st.st_size==524288)bios=b;
    if(ec)message=tr("部分目录无法读取","Some folders could not be read");render();
}
lv_obj_t *label(const std::string &s,int x,int y,int w,uint32_t c=0xeaf0f6){
    auto *o=lv_label_create(lv_screen_active());lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_color(o,lv_color_hex(c),0);return o;
}
void button(const char *s,int x,int y,int w,int id){
    auto *o=lv_button_create(lv_screen_active());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,44);lv_obj_set_style_bg_color(o,lv_color_hex(0x293e55),0);lv_obj_set_style_shadow_width(o,0,0);
    auto *l=lv_label_create(o);lv_label_set_text(l,s);lv_obj_center(l);
    lv_obj_add_event_cb(o,[](lv_event_t *e){int id=int(intptr_t(lv_event_get_user_data(e)));
        if(id==0)start();if(id==1){muted=!muted;settings();render();}if(id==2){interpreter=!interpreter;settings();render();}if(id==3)scan();
        if(id==4&&selected>=5){selected-=5;render();}if(id==5&&selected+5<games.size()){selected+=5;render();}
    },LV_EVENT_CLICKED,(void*)intptr_t(id));
}
void render(){
    auto *root=lv_screen_active();lv_obj_clean(root);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(root,lv_color_hex(0x111c2b),0);
    lv_obj_set_style_text_color(root,lv_color_hex(0xeaf0f6),0);if(font)lv_obj_set_style_text_font(root,font,0);
    label("PCSX4all",18,12,230);label(tr("W/S 选择 · 回车开始 · R 刷新","W/S select · Enter start · R refresh"),235,13,548,0xa8bccf);
    size_t first=(selected/5)*5;
    if(games.empty()){
        label(tr("还没有游戏镜像","No games yet"),24,70,490,0x80d2cf);
        label(tr("请将游戏复制到下方目录。\n多轨光盘请选择 .cue 文件。\n支持 BIN/CUE、ISO、IMG、PBP。","Copy games to the folder below.\nChoose .cue for multi-track discs.\nBIN/CUE, ISO, IMG, PBP supported."),24,116,490);
    }else for(size_t i=first;i<std::min(first+5,games.size());++i){
        auto *o=lv_button_create(root);lv_obj_set_pos(o,16,53+46*int(i-first));lv_obj_set_size(o,510,44);lv_obj_set_style_shadow_width(o,0,0);
        lv_obj_set_style_bg_color(o,lv_color_hex(i==selected?0x276977:0x23344a),0);
        auto *l=lv_label_create(o);auto name=fs::relative(games[i],folder).string();lv_label_set_text(l,name.c_str());lv_obj_set_width(l,480);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_center(l);
        lv_obj_add_event_cb(o,[](lv_event_t *e){selected=size_t(uintptr_t(lv_event_get_user_data(e)));render();},LV_EVENT_CLICKED,(void*)uintptr_t(i));
    }
    button(tr("开始游戏","Start game"),548,53,236,0);
    button(muted?tr("V  声音关闭","V  Sound off"):tr("V  声音开启","V  Sound on"),548,105,236,1);
    button(interpreter?tr("C  兼容模式","C  Interpreter"):tr("C  快速模式","C  Dynamic CPU"),548,157,236,2);
    button(tr("R  刷新列表","R  Refresh"),548,209,236,3);
    label(bios.empty()?tr("BIOS: HLE · 兼容有限","BIOS: HLE (limited)"):tr("已使用自备 BIOS","Using your BIOS"),548,269,242,0xa8bccf);
    button("<",16,284,50,4);button(">",76,284,50,5);
    std::string count=games.empty()?"0 / 0":std::to_string(selected+1)+" / "+std::to_string(games.size());label(count,144,292,100,0xa8bccf);
    label(message.empty()?"/storage/apps/data/pcsx4all/roms":message,250,306,535,0xa8bccf);
}
}
int main(){
    signal(SIGINT,signal_stop);signal(SIGTERM,signal_stop);umask(0077);
    fs::create_directories(c1::data()+"/pcsx4all/roms");fs::create_directories(c1::data()+"/pcsx4all/bios");folder=c1::data()+"/pcsx4all/roms";
    try{auto s=Json::parse(c1::read_file(c1::data()+"/pcsx4all/settings.json",4096));muted=s.value("muted",true);interpreter=s.value("interpreter",false);}catch(...){}
    if(!screen::open())return 1;font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),20);chinese=font;if(getenv("C1_PSX_ERROR")){message=tr("载入失败，请检查镜像 / BIOS","Load failed; check image / BIOS");unsetenv("C1_PSX_ERROR");}scan();
    while(!stop&&!screen::quit){lv_timer_handler();for(uint32_t k;(k=screen::take_key());){
        if(k==screen::KEY_HOME){screen::quit=true;break;}if(k>='A'&&k<='Z')k+='a'-'A';
        if(k==LV_KEY_ENTER){start();continue;}if(k=='w'&&selected)selected--;if(k=='s'&&selected+1<games.size())selected++;
        if(k=='r'){scan();continue;}if(k=='v'){muted=!muted;settings();}if(k=='c'){interpreter=!interpreter;settings();}render();
    }usleep(12000);}
    lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);if(font)lv_tiny_ttf_destroy(font);screen::close();return 0;
}
