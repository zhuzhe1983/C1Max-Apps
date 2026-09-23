#include "game.hpp"
#include "display.hpp"
#include "net.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <algorithm>
#include <array>
#include <csignal>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t stopped=0;
void stop(int){stopped=1;}
gomoku::Game game;
constexpr int size=300, origin=17, step=19;
std::array<uint32_t,size*size> pixels;
lv_image_dsc_t image{};
lv_obj_t *board, *status, *detail, *notice, *mode_label, *confirm=nullptr;
lv_font_t *font=nullptr;
int cursor_x=7,cursor_y=7,pending=0;
uint32_t ai_at=0;
std::string save_path;
bool chinese=true;
const char *tr(const char *cn,const char *en){return chinese?cn:en;}
lv_obj_t *label(const char *s,int x,int y,int w,uint32_t color=0xe9eef5,lv_obj_t *parent=nullptr){
    auto *o=lv_label_create(parent?parent:lv_screen_active());lv_label_set_text(o,s);
    lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_obj_set_style_text_color(o,lv_color_hex(color),0);return o;
}
void save(){
    try{c1::save_private(save_path,game.serialize());lv_label_set_text(notice,tr("棋局自动保存","Game saved automatically"));}
    catch(const std::exception &){lv_label_set_text(notice,tr("保存失败，请检查存储空间","Save failed; check storage"));}
}
void pixel(int x,int y,uint32_t c){if(x>=0&&y>=0&&x<size&&y<size)pixels[y*size+x]=0xff000000|c;}
void line(int x0,int y0,int x1,int y1,uint32_t c){
    const int dx=std::abs(x1-x0),sx=x0<x1?1:-1,dy=-std::abs(y1-y0),sy=y0<y1?1:-1;int err=dx+dy;
    for(;;){pixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e=err*2;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
void circle(int x,int y,int r,uint32_t c){for(int j=-r;j<=r;++j)for(int i=-r;i<=r;++i)if(i*i+j*j<=r*r)pixel(x+i,y+j,c);}
void refresh(){
    pixels.fill(0xffdbbd87);
    for(int i=0;i<15;++i){line(origin+i*step,origin,origin+i*step,origin+14*step,0x9e814f);line(origin,origin+i*step,origin+14*step,origin+i*step,0x9e814f);}
    for(auto p:std::array<gomoku::Move,5>{{{3,3},{11,3},{7,7},{3,11},{11,11}}})circle(origin+p.x*step,origin+p.y*step,2,0x766039);
    for(int y=0;y<15;++y)for(int x=0;x<15;++x)if(int p=game.at(x,y)){
        circle(origin+x*step+1,origin+y*step+1,8,0xa58b61);
        circle(origin+x*step,origin+y*step,8,p==1?0x19212b:0xf9faf5);
        circle(origin+x*step-2,origin+y*step-2,2,p==1?0x354351:0xffffff);
    }
    if(!game.moves.empty()){auto m=game.moves.back();circle(origin+m.x*step,origin+m.y*step,2,0xd6613e);}
    const int x=origin+cursor_x*step,y=origin+cursor_y*step;
    for(int a:{-1,1})for(int b:{-1,1}){line(x+a*9,y+b*9,x+a*4,y+b*9,0x126b80);line(x+a*9,y+b*9,x+a*9,y+b*4,0x126b80);}
    lv_obj_invalidate(board);
    const char *s=game.winner==3?tr("平局","Draw"):game.winner==1?tr("黑方获胜","Black wins"):game.winner==2?tr("白方获胜","White wins"):
        game.turn()==1?tr("轮到黑方","Black to move"):game.computer?tr("电脑思考中…","Computer is thinking…"):tr("轮到白方","White to move");
    lv_label_set_text(status,s);
    char buf[160];std::snprintf(buf,sizeof buf,tr("第 %zu 手  ·  光标 %c%d","Move %zu  ·  Cursor %c%d"),game.moves.size()+(!game.winner),char('A'+cursor_x),cursor_y+1);
    lv_label_set_text(detail,buf);
    lv_label_set_text(mode_label,game.computer?tr("M  人机对弈","M  Human vs AI"):tr("M  双人对弈","M  Two players"));
    if(!game.winner&&game.computer&&game.turn()==2&&!ai_at)ai_at=screen::tick()+250;
}
void dismiss(){if(confirm){lv_obj_delete(confirm);confirm=nullptr;}pending=0;}
void reset(){bool computer=game.computer;if(pending==2)computer=!computer;dismiss();game=gomoku::Game{};game.computer=computer;ai_at=0;cursor_x=cursor_y=7;save();refresh();}
void place(){if(confirm||game.winner||(game.computer&&game.turn()==2))return;if(game.place(cursor_x,cursor_y)){save();refresh();}}
void action(int id);
lv_obj_t *button(const char *text,int x,int y,int w,int id,lv_obj_t *parent=nullptr){
    auto *o=lv_button_create(parent?parent:lv_screen_active());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,46);
    lv_obj_set_style_bg_color(o,lv_color_hex(id==0?0x287887:0x293c52),0);lv_obj_set_style_shadow_width(o,0,0);lv_obj_set_style_radius(o,9,0);
    auto *l=lv_label_create(o);lv_label_set_text(l,text);lv_obj_center(l);
    lv_obj_add_event_cb(o,[](lv_event_t *e){action(int(intptr_t(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,(void*)intptr_t(id));return l;
}
void request_reset(int type){
    if(confirm)return;pending=type;if(game.moves.empty()){reset();return;}
    confirm=lv_obj_create(lv_screen_active());lv_obj_set_pos(confirm,335,65);lv_obj_set_size(confirm,445,209);lv_obj_remove_flag(confirm,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(confirm,lv_color_hex(0x1b2b3e),0);lv_obj_set_style_border_color(confirm,lv_color_hex(0x558396),0);lv_obj_set_style_pad_all(confirm,0,0);
    label(tr("开始新棋局？","Start a new game?"),20,18,400,0xffffff,confirm);
    label(tr("当前棋局会被替换。","This will replace the saved game."),20,56,400,0xafbdcc,confirm);
    button(tr("回车  确认","Enter  Confirm"),20,142,193,4,confirm);button(tr("返回  取消","Back  Cancel"),228,142,193,5,confirm);
}
void action(int id){
    if(id==4){reset();return;}if(id==5){dismiss();return;}if(confirm)return;
    if(id==0)place();else if(id==1){ai_at=0;if(game.undo()){save();refresh();}}
    else if(id==2)request_reset(1);else if(id==3)request_reset(2);
}
void touch(lv_event_t *){
    if(confirm)return;lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);
    cursor_x=std::clamp((int(p.x)-15-origin+step/2)/step,0,14);
    cursor_y=std::clamp((int(p.y)-25-origin+step/2)/step,0,14);refresh();
}
void key(uint32_t k){
    if(k==screen::KEY_HOME){screen::quit=true;return;}
    if(confirm){if(k==LV_KEY_ENTER)reset();else if(k==screen::KEY_EXIT||k==LV_KEY_ESC)dismiss();return;}
    if(k>='A'&&k<='Z')k+='a'-'A';
    if(k==LV_KEY_ENTER||k==' ')place();else if(k=='u')action(1);else if(k=='n')action(2);else if(k=='m')action(3);
    else {if(k=='a'||k==LV_KEY_LEFT)cursor_x=std::max(0,cursor_x-1);else if(k=='d'||k==LV_KEY_RIGHT)cursor_x=std::min(14,cursor_x+1);
        else if(k=='w'||k==LV_KEY_UP)cursor_y=std::max(0,cursor_y-1);else if(k=='s'||k==LV_KEY_DOWN)cursor_y=std::min(14,cursor_y+1);else return;refresh();}
}
}
int main(){
    std::signal(SIGTERM,stop);std::signal(SIGINT,stop);
    mkdir(c1::data().c_str(),0700);mkdir((c1::data()+"/gomoku").c_str(),0700);
    save_path=c1::data()+"/gomoku/game.txt";std::string error;
    if(access(save_path.c_str(),F_OK)==0){try{game=gomoku::Game::parse(c1::read_file(save_path,4096));}catch(const std::exception &){error="Saved game unreadable; new game ready";}}
    if(!screen::open())return 1;
    font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),21);chinese=font;
    auto *root=lv_screen_active();if(font)lv_obj_set_style_text_font(root,font,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x111c2b),0);lv_obj_set_style_text_color(root,lv_color_hex(0xe9eef5),0);
    image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_ARGB8888;image.header.w=size;image.header.h=size;image.header.stride=size*4;
    image.data_size=pixels.size()*4;image.data=(uint8_t*)pixels.data();
    board=lv_image_create(root);lv_image_set_src(board,&image);lv_obj_set_pos(board,15,25);lv_obj_add_flag(board,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board,touch,LV_EVENT_CLICKED,nullptr);
    label(tr("五子棋","Gomoku"),340,20,435);status=label("",340,58,435,0x80d2cf);detail=label("",340,94,435,0xacbecf);
    button(tr("回车  落子","Enter  Place"),340,133,211,0);button(tr("U  悔棋","U  Undo"),563,133,211,1);
    button(tr("N  新棋局","N  New game"),340,191,211,2);mode_label=button("",563,191,211,3);
    label(tr("WASD 移动 · 点击棋盘选择，再按落子","WASD / Tap board to select, then Place"),340,254,445,0xacbecf);
    notice=label(error.empty()?tr("连成五子获胜 · 无禁手 · 自动存档","Five in a row · Freestyle · Autosave"):error.c_str(),340,295,445,0xacbecf);refresh();
    while(!stopped&&!screen::quit){lv_timer_handler();for(uint32_t k;(k=screen::take_key());)key(k);
        if(ai_at&&!confirm&&int32_t(screen::tick()-ai_at)>=0){ai_at=0;if(!game.winner&&game.computer&&game.turn()==2){auto m=game.best_move();if(game.place(m.x,m.y))save();refresh();}}
        usleep(10000);
    }
    lv_obj_clean(root);lv_obj_set_style_text_font(root,LV_FONT_DEFAULT,0);if(font)lv_tiny_ttf_destroy(font);screen::close();return 0;
}
