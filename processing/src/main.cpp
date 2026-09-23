#include "runtime.hpp"
#include "display.hpp"
#include "net.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <algorithm>
#include <csignal>
#include <cstring>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
namespace {
enum class Page{Library,Run,Edit,Help,Error};
Page page=Page::Library;
volatile sig_atomic_t stopped=0;
void stop(int){stopped=1;}
sketch::Runtime runtime;
lv_font_t *font=nullptr,*mono=nullptr;
lv_obj_t *canvas=nullptr,*editor=nullptr,*hint=nullptr,*pause_label=nullptr;
lv_image_dsc_t descriptor{};
std::string api,source,title,problem;
bool paused=false,chinese=true;
int prefix=0;
uint32_t last_frame=0;
const char *tr(const char *cn,const char *en){return chinese?cn:en;}
void library();void run();void edit();void help();void error(const std::string&);
lv_obj_t *label(const std::string &s,int x,int y,int w,uint32_t c=0xe9eff5){auto *o=lv_label_create(lv_screen_active());lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_color(o,lv_color_hex(c),0);return o;}
void clear(Page p){page=p;canvas=editor=hint=pause_label=nullptr;lv_obj_clean(lv_screen_active());auto *r=lv_screen_active();lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(r,lv_color_hex(0x101b2a),0);lv_obj_set_style_text_color(r,lv_color_hex(0xe9eff5),0);if(font)lv_obj_set_style_text_font(r,font,0);}
bool save(){
    if(editor)source=lv_textarea_get_text(editor);
    try{c1::save_private(c1::data()+"/processing/my-sketch.js",source);return true;}
    catch(const std::exception &){if(hint)lv_label_set_text(hint,tr("保存失败，请检查存储空间","Save failed; check storage"));return false;}
}
void select(int id){
    static const char *names[]={"tree","koch","flocking","particles"};
    static const char *cn[]={"树形分形","Koch 曲线","群聚模拟","粒子系统"};
    try{
        if(id<4){source=c1::read_file(c1::root()+"/processing/examples/"+names[id]+".js",16384);title=chinese?cn[id]:names[id];}
        else {title=tr("我的程序","My sketch");try{source=c1::read_file(c1::data()+"/processing/my-sketch.js",65536);}catch(...){source="// My sketch: change the numbers, then Run.\nfunction draw() {\n  background(17,25,40);\n  noFill(); stroke(110,210,220);\n  translate(width/2,height/2);\n  for(let i=0;i<120;i++) {\n    let t=i*0.06+frameCount*0.02;\n    ellipse(sin(t*3)*130,cos(t*2)*45,5,5);\n  }\n}\n";}}
        run();
    }catch(const std::exception &e){error(e.what());}
}
void action(int id){
    if(id>=0&&id<5){select(id);return;}
    if(id==5){help();return;}
    if(id==6){if(page==Page::Edit&&!save())return;library();return;}
    if(id==7){paused=!paused;if(hint)lv_label_set_text(hint,paused?tr("已暂停 · 点击继续","Paused · Tap to resume"):title.c_str());if(pause_label)lv_label_set_text(pause_label,paused?tr("P 继续","P Resume"):tr("P 暂停","P Pause"));return;}
    if(id==8){run();return;}
    if(id==9){edit();return;}
    if(id==10){if(save())run();return;}
}
lv_obj_t *button(const char *s,int x,int y,int w,int h,int id){auto *o=lv_button_create(lv_screen_active());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(0x284155),0);lv_obj_set_style_shadow_width(o,0,0);auto *l=lv_label_create(o);lv_label_set_text(l,s);lv_obj_center(l);lv_obj_add_event_cb(o,[](lv_event_t *e){action(int(intptr_t(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,(void*)intptr_t(id));return l;}
void library(){
    clear(Page::Library);label("Processing 2D",18,12,250);label(tr("轻量 JavaScript 兼容版","Lightweight JavaScript subset"),325,15,456,0x9fb7cd);
    const char *names[]={tr("Q  树形分形","Q  Recursive tree"),tr("W  Koch 曲线","W  Koch curve"),tr("E  群聚模拟","E  Flocking"),tr("R  粒子系统","R  Particle systems"),tr("T  我的程序","T  My sketch"),tr("H  编程帮助","H  Coding help")};
    for(int i=0;i<6;i++)button(names[i],18+(i%2)*390,59+(i/2)*83,374,65,i);
    label(tr("前四项改编自 Processing 官方示例 · Daniel Shiffman","Four adapted official examples · Daniel Shiffman"),18,307,765,0x9fb7cd);
}
void run(){
    clear(Page::Run);paused=false;last_frame=0;
    if(!runtime.load(api,source)){error(runtime.error);return;}
    descriptor={};descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;descriptor.header.cf=LV_COLOR_FORMAT_ARGB8888;descriptor.header.w=400;descriptor.header.h=145;descriptor.header.stride=1600;descriptor.data_size=runtime.pixels.size()*4;descriptor.data=(uint8_t*)runtime.pixels.data();
    canvas=lv_image_create(lv_screen_active());lv_image_set_src(canvas,&descriptor);lv_obj_set_pos(canvas,0,50);lv_image_set_pivot(canvas,0,0);lv_image_set_scale(canvas,512);
    // Image zoom does not enlarge LVGL's hit box; cover the displayed canvas.
    auto *touch=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(touch);lv_obj_set_pos(touch,0,50);lv_obj_set_size(touch,800,290);lv_obj_remove_flag(touch,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(touch,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(touch,[](lv_event_t *e){auto code=lv_event_get_code(e);if(code!=LV_EVENT_PRESSED&&code!=LV_EVENT_PRESSING&&code!=LV_EVENT_RELEASED)return;lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);if(!runtime.pointer(p.x/2,(p.y-50)/2,code!=LV_EVENT_RELEASED))error(runtime.error);},LV_EVENT_ALL,nullptr);
    button(tr("示例","Demos"),5,3,85,44,6);hint=button(title.c_str(),99,3,298,44,7);
    pause_label=button(tr("P 暂停","P Pause"),406,3,122,44,7);button(tr("R 重置","R Reset"),537,3,122,44,8);button(tr("E 编辑","E Edit"),668,3,126,44,9);
}
void editor_hint(){
    if(!hint)return;const char *s=prefix==1?tr("导航：WASD 移动 · Q/E 文首/尾 · T 缩进","NAV: WASD arrows / Q,E start,end / T tab"):
        prefix==2?"Q[ W] E{ R} T< Y> U= I+ O_ P\\ A\" S' D` F! G| H^":
        tr("相机键：一次导航，两次符号 · 返回保存","Camera: NAV / symbols; Back saves");
    std::string mode=std::string(screen::caps_lock()?"ABC · ":"abc · ")+s;lv_label_set_text(hint,mode.c_str());
}
void edit(){
    clear(Page::Edit);prefix=0;button(tr("保存返回","Save / Back"),8,3,148,44,6);label(tr("我的程序 · 实体键盘输入","My sketch · Physical keyboard"),176,14,400);button(tr("保存并运行","Save and Run"),604,3,188,44,10);
    editor=lv_textarea_create(lv_screen_active());lv_obj_set_pos(editor,8,54);lv_obj_set_size(editor,784,248);lv_textarea_set_max_length(editor,16384);lv_textarea_set_one_line(editor,false);lv_textarea_set_text(editor,source.c_str());lv_textarea_set_cursor_pos(editor,LV_TEXTAREA_CURSOR_LAST);
    lv_obj_set_style_bg_color(editor,lv_color_hex(0x172638),0);lv_obj_set_style_text_color(editor,lv_color_hex(0xeaf3fb),0);if(mono)lv_obj_set_style_text_font(editor,mono,0);
    hint=label("",10,312,782,0xa7bfd0);editor_hint();
}
void help(){
    clear(Page::Help);button(tr("示例","Demos"),8,3,85,44,6);label(tr("Processing 风格的 2D 编程","Processing-style 2D sketches"),115,14,665);
    label(tr("使用 JavaScript 的 setup() / draw()；画布固定 400×145。\n提供线条、矩形、椭圆、三角形、颜色、旋转、平移和 PVector。\n点击画布触发 mousePressed()；拖动更新 mouseX / mouseY。\n运行时 P/空格暂停，R 重置，E 编辑；返回键回示例。\n编辑时双击 Shift 切换大小写；右上退格删除，回车换行。\n相机键一次进入导航，连续两次进入符号，按字母完成输入。\n完整 API 与示例来源见仓库 processing/README.md。\n不支持 Java .pde、P3D/WebGL、浏览器 DOM 和外部库。",
        "JavaScript setup() / draw(); fixed 400x145 canvas.\nLines, rectangles, ellipses, triangles, transforms and PVector.\nTap: mousePressed(); drag: mouseX / mouseY.\nP/Space pause, R reset, E edit; Back returns to demos.\nDouble Shift toggles capitals; Backspace deletes; Enter newline.\nCamera once: navigation; twice: punctuation; then a letter.\nSee processing/README.md for the API and example sources.\nNo Java .pde, P3D/WebGL, DOM or external libraries."),18,61,765,0xb7cbdc);
}
void error(const std::string &s){problem=s;clear(Page::Error);label(tr("程序已停止","Sketch stopped"),18,16,760,0xffbea6);auto *o=label(problem,18,60,762,0xd8e4ef);if(mono)lv_obj_set_style_text_font(o,mono,0);lv_obj_set_height(o,195);lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);button(tr("E  编辑修正","E  Edit sketch"),18,280,360,44,9);button(tr("返回示例","Back to demos"),397,280,386,44,6);}
void key(uint32_t k){
    if(k==screen::KEY_HOME){if(page==Page::Edit)save();screen::quit=true;return;}
    if(page==Page::Edit){
        if(k==screen::KEY_EXIT){if(prefix){prefix=0;editor_hint();}else if(save())library();return;}
        if(k==screen::KEY_SYMBOL){prefix=(prefix+1)%3;editor_hint();return;}
        if(k==screen::KEY_MODE){editor_hint();return;}
        uint32_t lower=k>='A'&&k<='Z'?k+32:k;
        if(prefix==1){prefix=0;if(lower=='a')lv_textarea_cursor_left(editor);if(lower=='d')lv_textarea_cursor_right(editor);if(lower=='w')lv_textarea_cursor_up(editor);if(lower=='s')lv_textarea_cursor_down(editor);if(lower=='q')lv_textarea_set_cursor_pos(editor,0);if(lower=='e')lv_textarea_set_cursor_pos(editor,LV_TEXTAREA_CURSOR_LAST);if(lower=='t')lv_textarea_add_text(editor,"  ");editor_hint();return;}
        if(prefix==2){prefix=0;const char *keys="qwertyuiopasdfgh",*values="[]{}<>=+_\\\"'`!|^";auto *p=lower<128?strchr(keys,char(lower)):nullptr;if(p){char s[2]={values[p-keys],0};lv_textarea_add_text(editor,s);}editor_hint();return;}
        if(k==LV_KEY_BACKSPACE)lv_textarea_delete_char(editor);else if(k==LV_KEY_ENTER)lv_textarea_add_text(editor,"\n");else if(k==LV_KEY_LEFT)lv_textarea_cursor_left(editor);else if(k==LV_KEY_RIGHT)lv_textarea_cursor_right(editor);else if(k==LV_KEY_UP)lv_textarea_cursor_up(editor);else if(k==LV_KEY_DOWN)lv_textarea_cursor_down(editor);else if(k>=32&&k<127){char s[2]={char(k),0};lv_textarea_add_text(editor,s);}return;
    }
    if(k==screen::KEY_EXIT){library();return;}if(k>='A'&&k<='Z')k+=32;
    if(page==Page::Library){const char *keys="qwert";auto *p=k<128?strchr(keys,char(k)):nullptr;if(p)select(p-keys);else if(k=='h')help();return;}
    if(page==Page::Error){if(k=='e')edit();return;}
    if(page==Page::Run){if(k=='p'||k==' ')action(7);else if(k=='r')run();else if(k=='e')edit();else if(k>=32&&k<127&&!runtime.key(char(k)))error(runtime.error);}
}
}
int main(){
    signal(SIGINT,stop);signal(SIGTERM,stop);mkdir(c1::data().c_str(),0700);mkdir((c1::data()+"/processing").c_str(),0700);
    if(!screen::open())return 1;font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),20);mono=lv_tiny_ttf_create_file(("A:"+c1::root()+"/terminal/assets/JetBrainsMono-Regular.ttf").c_str(),16);chinese=font;
    try{api=c1::read_file(c1::root()+"/processing/api.js",32768);library();}catch(const std::exception &e){error(e.what());}
    while(!stopped&&!screen::quit){lv_timer_handler();for(uint32_t k;(k=screen::take_key());)key(k);
        if(page==Page::Run&&!paused&&uint32_t(screen::tick()-last_frame)>=66){last_frame=screen::tick();if(runtime.step())lv_obj_invalidate(canvas);else error(runtime.error);}
        usleep(8000);
    }
    if(page==Page::Edit)save();lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);if(mono)lv_tiny_ttf_destroy(mono);if(font)lv_tiny_ttf_destroy(font);screen::close();return 0;
}
