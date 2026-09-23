// GPL-2.0-or-later.
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
bool muted=true,game=true,stretch=false,chinese=true,help=false;
unsigned cycles=2750,memory=8;
std::string folder,message;
lv_font_t *font=nullptr;
const char *tr(const char *cn,const char *en){return chinese?cn:en;}
void render();
void settings(){try{c1::save_private(c1::data()+"/dosbox/settings.json",Json{{"muted",muted},{"game_keys",game},{"stretch",stretch},{"cycles",cycles},{"memory",memory}}.dump());}catch(...){message=tr("设置保存失败","Could not save settings");}}
void start(bool shell=false){
    if(!shell&&games.empty())return;settings();
    std::string executable=c1::root()+"/dosbox/c1max-dos-core";
    if(access(executable.c_str(),X_OK)){message=tr("模拟器文件缺失，请重新安装","Emulator missing; reinstall app");render();return;}
    std::vector<std::string> args={executable};if(shell)args.push_back("--shell");else{args.push_back("--file");args.push_back(games[selected]);if(game)args.push_back("--game");}
    args.push_back(muted?"--mute":"--audio");args.push_back("--cycles");args.push_back(std::to_string(cycles));args.push_back("--memory");args.push_back(std::to_string(memory));if(stretch||shell)args.push_back("--stretch");
    std::vector<char*> av;for(auto &a:args)av.push_back(a.data());av.push_back(nullptr);
    screen::close();execv(executable.c_str(),av.data());_exit(1);
}
void scan(){
    games.clear();std::error_code ec;fs::recursive_directory_iterator it(folder,fs::directory_options::skip_permission_denied,ec),end;
    while(!ec&&it!=end&&games.size()<200){
        if(it.depth()>=3)it.disable_recursion_pending();
        if(it->is_regular_file(ec)){auto ext=it->path().extension().string();std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return std::tolower(c);});if(ext==".exe"||ext==".com"||ext==".bat")games.push_back(it->path().string());}
        it.increment(ec);
    }
    std::sort(games.begin(),games.end());selected=std::min(selected,games.empty()?size_t(0):games.size()-1);if(ec)message=tr("部分目录无法读取","Some folders could not be read");render();
}
lv_obj_t *label(const std::string &s,int x,int y,int w,uint32_t c=0xeaf0f6){auto *o=lv_label_create(lv_screen_active());lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_color(o,lv_color_hex(c),0);return o;}
void action(int id){
    if(id==0){start();return;}if(id==1){start(true);return;}if(id==2)game=!game;if(id==3)help=!help;if(id==4)muted=!muted;if(id==5)cycles=cycles==2750?4720:cycles==4720?7800:2750;if(id==6)memory=memory==8?16:8;if(id==7)stretch=!stretch;
    if(id==8){scan();return;}if(id==9&&selected>=5)selected-=5;if(id==10&&selected+5<games.size())selected+=5;settings();render();
}
void button(const char *s,int x,int y,int w,int id){auto *o=lv_button_create(lv_screen_active());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,42);lv_obj_set_style_bg_color(o,lv_color_hex(0x293e55),0);lv_obj_set_style_shadow_width(o,0,0);auto *l=lv_label_create(o);lv_label_set_text(l,s);lv_obj_center(l);lv_obj_add_event_cb(o,[](lv_event_t *e){action(int(intptr_t(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,(void*)intptr_t(id));}
void render(){
    auto *root=lv_screen_active();lv_obj_clean(root);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(root,lv_color_hex(0x111c2b),0);lv_obj_set_style_text_color(root,lv_color_hex(0xeaf0f6),0);if(font)lv_obj_set_style_text_font(root,font,0);
    label("DOSBox",18,12,175);label(help?tr("H 返回游戏库 · 电源返回菜单","H library · Power home"):tr("W/S 选择 · 回车开始 · R 刷新","W/S select · Enter start · R refresh"),205,13,575,0xa8bccf);
    if(help){
        label(tr("长按拍照键：暂停菜单；电源键：回 launcher\n文本模式：Shift 输入数字/符号，双击 Shift 切换大写\n右上退格 / 中间 Esc / 右下回车；触摸作为鼠标左键\n短按拍照键切换一次性前缀：方向 → F键 → Ctrl → Alt → 符号\n方向：WASD；F1–F12：QWERTYUIOPAS\n游戏模式：WASD 方向；J Ctrl、K Alt、U 空格、I 回车",
        "Hold Camera: pause menu. Power: launcher.\nText: Shift numbers/symbols; double Shift CAPS.\nBackspace / Esc / Enter. Touch = left mouse button.\nTap Camera: NAV -> F1-F12 -> Ctrl -> Alt -> symbols.\nNAV: WASD; F1-F12: QWERTYUIOPAS.\nGame: WASD arrows, J Ctrl, K Alt, U Space, I Enter."),18,52,766,0xd7e4ed);
        button(muted?tr("V 声音关闭","V Sound off"):tr("V 声音开启","V Sound on"),18,240,178,4);
        button((std::string("C ")+std::to_string(cycles)+" cycles").c_str(),210,240,178,5);
        button((std::string("M ")+std::to_string(memory)+" MB").c_str(),402,240,178,6);
        button(stretch?tr("T 铺满宽度","T Fill width"):"T 4:3",594,240,190,7);
        label(tr("建议先运行内置 DOS LAB；首版以早期 2D DOS 程序为主。","Try DOS LAB first. Best suited to early 2D DOS programs."),18,304,766,0xa8bccf);return;
    }
    size_t first=(selected/5)*5;
    if(games.empty())label(tr("将 DOS 游戏完整目录放在下方路径。\n启动文件使用英文 8.3 文件名。","Copy a complete DOS game folder below.\nUse ASCII 8.3 executable names."),24,80,485);
    else for(size_t i=first;i<std::min(first+5,games.size());i++){
        auto *o=lv_button_create(root);lv_obj_set_pos(o,16,53+45*int(i-first));lv_obj_set_size(o,510,42);lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_bg_color(o,lv_color_hex(i==selected?0x276977:0x23344a),0);
        auto *l=lv_label_create(o);auto name=fs::relative(games[i],folder).string();lv_label_set_text(l,name.c_str());lv_obj_set_width(l,480);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_center(l);
        lv_obj_add_event_cb(o,[](lv_event_t *e){selected=size_t(uintptr_t(lv_event_get_user_data(e)));render();},LV_EVENT_CLICKED,(void*)uintptr_t(i));
    }
    button(tr("开始程序","Start program"),548,53,236,0);button(tr("D  DOS 命令行","D  DOS shell"),548,103,236,1);button(game?tr("G  游戏键盘","G  Game keyboard"):tr("G  文本键盘","G  Text keyboard"),548,153,236,2);button(tr("H  按键与设置","H  Keys / settings"),548,203,236,3);
    label(muted?tr("静音 · 解释器","Muted · Interpreter"):tr("声音开启 · 解释器","Audio on · Interpreter"),548,262,242,0xa8bccf);
    button("<",16,281,50,9);button(">",76,281,50,10);label(games.empty()?"0 / 0":std::to_string(selected+1)+" / "+std::to_string(games.size()),144,291,100,0xa8bccf);
    label(message.empty()?"/storage/apps/data/dosbox/games":message,250,305,535,0xa8bccf);
}
}
int main(){
    signal(SIGINT,signal_stop);signal(SIGTERM,signal_stop);umask(0077);folder=c1::data()+"/dosbox/games";
    std::error_code ec;fs::create_directories(folder+"/C1LAB",ec);if(ec)return 1;
    fs::copy_file(c1::root()+"/dosbox/C1LAB.COM",folder+"/C1LAB/C1LAB.COM",fs::copy_options::skip_existing,ec);
    try{auto s=Json::parse(c1::read_file(c1::data()+"/dosbox/settings.json",4096));muted=s.value("muted",true);game=s.value("game_keys",true);stretch=s.value("stretch",false);cycles=s.value("cycles",2750u);memory=s.value("memory",8u);}catch(...){}
    if(cycles!=2750&&cycles!=4720&&cycles!=7800)cycles=2750;if(memory!=8&&memory!=16)memory=8;
    if(!screen::open())return 1;font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),20);chinese=font;
    if(getenv("C1_DOS_ERROR")){message=getenv("C1_DOS_ERROR");unsetenv("C1_DOS_ERROR");}scan();
    while(!stop&&!screen::quit){lv_timer_handler();for(uint32_t k;(k=screen::take_key());){
        if(k==screen::KEY_HOME){screen::quit=true;break;}if(k>='A'&&k<='Z')k+='a'-'A';
        if(k=='h'||(k==screen::KEY_EXIT&&help)){action(3);continue;}
        if(help){if(k=='v')action(4);if(k=='c')action(5);if(k=='m')action(6);if(k=='t')action(7);continue;}
        if(k==LV_KEY_ENTER){start();continue;}if(k=='w'&&selected)selected--;if(k=='s'&&selected+1<games.size())selected++;
        if(k=='r'){scan();continue;}if(k=='g'){action(2);continue;}if(k=='d'){start(true);continue;}render();
    }usleep(12000);}
    lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);if(font)lv_tiny_ttf_destroy(font);screen::close();return 0;
}
