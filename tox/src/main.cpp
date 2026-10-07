#include "text_input.hpp"
// SPDX-License-Identifier: GPL-3.0-only
#include "engine.hpp"
#include "session.hpp"
#include "media.hpp"
#include <ctime>
#include "qr.hpp"
#include "scanner.hpp"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "display.hpp"
#include "net.hpp"
#include "lv_tiny_ttf.h"
#include <lvgl.h>
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace {
c1ime::TextInput text_input;
constexpr uint32_t bg=0x111b24, surface=0x1b2a35, raised=0x263945, ink=0xe6eee9, muted=0x9badae, accent=0x9bddbd, amber=0xf0c685;
constexpr uint32_t nav_profile=0x20001,nav_add=0x20002,nav_requests=0x20003,nav_scan=0x20004,nav_zoom1=0x20010,nav_zoom15=0x20011,nav_zoom2=0x20012;
lv_font_t*font=nullptr;
std::unique_ptr<chat::Session>engine;
std::unique_ptr<chat::Media>media,inline_audio;
struct BubblePreview {chat::Preview info;lv_image_dsc_t image{};};
std::map<std::string,BubblePreview>previews;
lv_obj_t*chat_history=nullptr;int chat_scroll=-1;unsigned rendered_offset=0;
bool scroll_shortcuts=false;
lv_image_dsc_t media_descriptor{};lv_obj_t*media_image=nullptr;
size_t file_index=0,queue_index=0;int queue_confirm=0;uint64_t queue_target=0;
constexpr uint32_t nav_background=0x20100,nav_photo=0x20101,nav_voice=0x20102,nav_files=0x20103,nav_capture=0x20104,nav_play=0x20105,nav_retry=0x20106,nav_queue=0x20107,nav_cancel_queued=0x20108,nav_resend=0x20109,nav_queue_yes=0x2010a,nav_queue_no=0x2010b,nav_queue_row=0x21100,nav_bubble=0x21200,nav_file_row=0x21000;
chat::Snapshot view;
enum class Page { Friends, Chat, Add, Profile, Rename, Requests, Delete, Scan, Files, Photo, Voice, Attachment, Queue };
Page page=Page::Friends,scan_origin=Page::Friends,attachment_origin=Page::Files;
std::unique_ptr<chat::Scanner>scanner;
chat::ScanView scan_view;
chat::QrImage own_qr;
std::string qr_id;
lv_image_dsc_t qr_descriptor{},scan_descriptor{};
lv_obj_t *scan_image=nullptr,*scan_status=nullptr,*scan_focus=nullptr;
bool scanned=false;

uint32_t selected=UINT32_MAX;
size_t request_index=0;
unsigned history_offset=0;
std::map<uint32_t,std::string>drafts;
std::string edit,notice;
uint64_t token=uint64_t(time(nullptr))*100000+getpid()%100000,pending=0;
uint64_t dismissed_error=0;
chat::Action pending_action=chat::Action::Read;
volatile sig_atomic_t stopped=0;
void stop_signal(int){stopped=1;}
void paint();
void key(uint32_t);
void label(lv_obj_t*p,const std::string&t,int x,int y,int w,int h,uint32_t color=ink){
    auto*o=lv_label_create(p);lv_label_set_text(o,t.c_str());lv_label_set_long_mode(o,LV_LABEL_LONG_MODE_WRAP);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_obj_set_style_text_font(o,font?font:LV_FONT_DEFAULT,0);
}
lv_obj_t*box(int x,int y,int w,int h,uint32_t color=surface){auto*o=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,12,0);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;}
lv_obj_t* button(const char*t,int x,int y,int w,uint32_t k,bool active=false){
    auto*b=box(x,y,w,34,active?accent:raised);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);label(b,t,10,6,w-18,23,active?bg:ink);
    lv_obj_add_event_cb(b,[](lv_event_t*e){key(uint32_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,reinterpret_cast<void*>(uintptr_t(k)));return b;
}
const chat::Friend*friend_now(){for(auto&f:view.friends)if(f.number==selected)return &f;return nullptr;}
std::string tail(const std::string&s,size_t bytes){if(s.size()<=bytes)return s;size_t at=s.size()-bytes;while(at<s.size()&&(static_cast<unsigned char>(s[at])&0xc0)==0x80)at++;return "…"+s.substr(at);}
std::string transfer_status(const std::string&s){if(s=="queued")return "待发送";if(s=="sending")return "发送中";if(s=="unconfirmed")return "未确认";if(s=="complete")return "已传输";if(s=="cancelled")return "已取消";if(s=="failed")return "失败";if(s=="waiting")return "等待接受";if(s=="receiving")return "接收中";return "未完成";}
std::vector<const chat::Message*> queue_items(){std::vector<const chat::Message*>v;for(auto&m:view.messages)if(m.mine&&m.id&&(m.state=="queued"||m.state=="unconfirmed"||m.state=="failed"))v.push_back(&m);return v;}
std::string text_status(const std::string&s){if(s=="delivered")return "已送达";if(s=="sent")return "已发出";return transfer_status(s);}
void status_icon(lv_obj_t*parent,const std::string&state,int x,int y,bool light=false){
    const bool delivered=state=="delivered",waiting=state=="queued"||state=="sending"||state=="waiting"||state=="receiving";
    const bool warning=state=="unconfirmed"||state=="failed";
    const uint32_t color=delivered?0x43c783:waiting||warning?(light?0x805400:amber):(light?0x455b61:muted);
    auto*icon=lv_obj_create(parent);lv_obj_remove_style_all(icon);lv_obj_set_pos(icon,x,y);lv_obj_set_size(icon,18,18);lv_obj_remove_flag(icon,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(icon,9,0);
    if(delivered){lv_obj_set_style_bg_color(icon,lv_color_hex(color),0);lv_obj_set_style_bg_opa(icon,LV_OPA_COVER,0);}
    else if(waiting||warning||state=="cancelled"){lv_obj_set_style_border_width(icon,2,0);lv_obj_set_style_border_color(icon,lv_color_hex(color),0);}
    static const lv_point_precise_t clock_hand[]={{9,4},{9,9},{13,9}},check[]={{4,9},{7,12},{14,5}},dash[]={{5,9},{13,9}},bang[]={{9,4},{9,10}},dot[]={{9,13},{9,14}};
    auto line=[&](const lv_point_precise_t*points,unsigned count){auto*o=lv_line_create(icon);lv_line_set_points(o,points,count);lv_obj_set_style_line_width(o,2,0);lv_obj_set_style_line_rounded(o,true,0);lv_obj_set_style_line_color(o,lv_color_hex(delivered?0x10291d:color),0);};
    if(waiting)line(clock_hand,3);else if(warning){line(bang,2);line(dot,2);}else if(state=="cancelled")line(dash,2);else line(check,3);
}
int chat_bubble(lv_obj_t*parent,const chat::Message&m,size_t index,int y,int width){
    BubblePreview*preview=nullptr;const bool available=!m.file.empty()&&(m.mine||m.state=="complete");
    if(available){auto it=previews.find(m.file);if(it==previews.end()){BubblePreview p;p.info=chat::preview(c1::data()+"/tox/media",m.file,m.kind);it=previews.emplace(m.file,std::move(p)).first;}preview=&it->second;}
    bool photo=preview&&m.kind=="photo"&&!preview->info.pixels.empty();
    bool voice=preview&&m.kind=="voice"&&preview->info.seconds;
    bool playing=inline_audio&&inline_audio->playing&&inline_audio->file==m.file;
    const std::string body=photo?"":voice?std::string(playing?"停止":"播放")+"  语音  "+std::to_string(preview->info.seconds)+" 秒":m.text+(m.file.empty()?"":" · 点开查看");
    lv_point_t size{};lv_text_get_size(&size,body.c_str(),font?font:LV_FONT_DEFAULT,0,3,510,LV_TEXT_FLAG_NONE);
    const int w=photo?preview->info.width+12:std::clamp<int>(size.x+24+(voice?30:0),58,534),h=photo?preview->info.height+12:std::max<int>(size.y+18,40),x=m.mine?width-w-14:14;
    const uint32_t fill=m.mine?0x9bddbd:0xdce5e8;
    auto*b=lv_obj_create(parent);lv_obj_remove_style_all(b);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);lv_obj_remove_flag(b,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_opa(b,LV_OPA_COVER,0);lv_obj_set_style_bg_color(b,lv_color_hex(fill),0);lv_obj_set_style_radius(b,11,0);
    if(photo){auto&i=preview->image;if(!i.data){i.header.magic=LV_IMAGE_HEADER_MAGIC;i.header.cf=LV_COLOR_FORMAT_ARGB8888;i.header.w=preview->info.width;i.header.h=preview->info.height;i.header.stride=i.header.w*4;i.data_size=preview->info.pixels.size()*4;i.data=reinterpret_cast<const uint8_t*>(preview->info.pixels.data());}auto*o=lv_image_create(b);lv_obj_set_pos(o,6,6);lv_image_set_src(o,&i);}
    else{label(b,body,voice?40:12,9,w-(voice?52:24),h-14,0x152a30);lv_obj_set_style_text_line_space(lv_obj_get_child(b,0),3,0);
        if(voice){static const lv_point_precise_t play[]={{3,2},{3,16},{15,9},{3,2}},pause1[]={{4,3},{4,15}},pause2[]={{12,3},{12,15}};auto line=[&](const lv_point_precise_t*p,unsigned n){auto*l=lv_line_create(b);lv_obj_set_pos(l,12,10);lv_line_set_points(l,p,n);lv_obj_set_style_line_width(l,playing?3:2,0);lv_obj_set_style_line_color(l,lv_color_hex(0x235545),0);};if(playing){line(pause1,2);line(pause2,2);}else line(play,4);}
    }
    if(available){lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(b,[](lv_event_t*e){key(nav_bubble+uint32_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,reinterpret_cast<void*>(index));}
    if(m.mine){int sx=x-111,sy=y+(h-24)/2;status_icon(parent,m.state,sx,sy+3);label(parent,text_status(m.state),sx+24,sy,83,26,m.state=="delivered"?0x43c783:m.state=="queued"||m.state=="unconfirmed"||m.state=="failed"?amber:muted);}
    return y+h+10;
}

void erase_utf8(std::string&s){if(s.empty())return;size_t n=s.size()-1;while(n>0&&(static_cast<unsigned char>(s[n])&0xc0)==0x80)n--;s.erase(n);}
void submit(chat::Command c){if(pending)return;c.token=++token;pending=c.token;pending_action=c.action;if(!engine->submit(c)){pending=0;notice="操作队列繁忙，请稍后再试";}}
void open_chat(uint32_t n){if(pending)return;selected=n;page=Page::Chat;history_offset=0;chat_scroll=-1;engine->submit({chat::Action::Read,n});view=engine->snapshot(selected);paint();}
void image_source(lv_obj_t*o,lv_image_dsc_t&d,const std::vector<uint32_t>&pixels,unsigned w,unsigned h){
    lv_image_cache_drop(&d);d={};d.header.magic=LV_IMAGE_HEADER_MAGIC;d.header.cf=LV_COLOR_FORMAT_ARGB8888;d.header.w=w;d.header.h=h;d.header.stride=w*4;d.data_size=pixels.size()*4;d.data=reinterpret_cast<const uint8_t*>(pixels.data());lv_image_set_src(o,&d);lv_obj_invalidate(o);
}
void stop_media(){media.reset();media_image=nullptr;lv_image_cache_drop(&media_descriptor);}
void stop_scan(){scanner.reset();scan_image=nullptr;scan_status=nullptr;scan_focus=nullptr;}
void start_scan(){
    if(!view.ready||view.id.empty()){notice="身份正在加载，请稍后再试";paint();return;}
    if(page==Page::Scan)return;
    scan_origin=page;scan_view={};scan_view.pixels.assign(chat::ScanView::width*chat::ScanView::height,0xff111b24);
    scanner=std::make_unique<chat::Scanner>(view.id);page=Page::Scan;paint();
}
void sidebar(){
    box(12,48,210,252);
    label(lv_screen_active(),"好友  "+std::to_string(view.friends.size())+" / 32",26,59,183,24,muted);
    size_t index=0;for(size_t i=0;i<view.friends.size();i++)if(view.friends[i].number==selected)index=i;
    size_t start=index>=4?index-3:0;
    for(size_t i=start;i<view.friends.size()&&i<start+4;i++){
        auto&f=view.friends[i];int y=91+int(i-start)*41;auto*b=box(20,y,194,38,f.number==selected?raised:surface);lv_obj_add_flag(b,LV_OBJ_FLAG_CLICKABLE);
        label(b,(f.online?"● ":"○ ")+f.name,8,7,170,24,f.online?accent:muted);
        if(f.unread)label(b,"+"+std::to_string(f.unread),145,7,46,24,amber);
        lv_obj_add_event_cb(b,[](lv_event_t*e){open_chat(uint32_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e))));},LV_EVENT_CLICKED,reinterpret_cast<void*>(uintptr_t(f.number)));
    }
    if(view.friends.empty())label(lv_screen_active(),"还没有好友\n添加对方的 Tox ID\n即可发送好友请求",28,103,176,88,muted);
    button("A 添加",24,257,84,nav_add);button("R 请求",116,257,94,nav_requests);
}
void paint(){
    const bool shortcuts=page==Page::Chat;
    if(shortcuts!=scroll_shortcuts){
        if(shortcuts){try{c1::save_private("/tmp/c1max-tox-scroll.pid",std::to_string(getpid())+"\n");scroll_shortcuts=true;}catch(...){notice="滚动快捷键注册失败，请重新打开应用";}}
        else{unlink("/tmp/c1max-tox-scroll.pid");scroll_shortcuts=false;}
    }
    auto*r=lv_screen_active();lv_obj_clean(r);chat_history=nullptr;
    if(page!=Page::Chat)inline_audio.reset();
    if(previews.size()>8){for(auto&[file,p]:previews)lv_image_cache_drop(&p.image);previews.clear();}media_image=nullptr;scan_image=nullptr;scan_status=nullptr;scan_focus=nullptr;lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(r,lv_color_hex(bg),0);lv_obj_set_style_text_font(r,font?font:LV_FONT_DEFAULT,0);
    label(r,"TOX",20,9,66,28,accent);label(r,view.name,90,10,182,24);
    label(r,view.status,278,10,225,24,view.online?accent:muted);
    button(view.background?"后台：开":"后台：关",509,4,147,nav_background,view.background);
    if(page==Page::Chat||page==Page::Queue)button(("队列 "+std::to_string(queue_items().size())).c_str(),668,4,118,nav_queue,page==Page::Queue);else button("我的 ID",668,4,118,nav_profile);
    std::string footer;
    if(page==Page::Friends||page==Page::Chat){
        auto*f=friend_now();
        if(page==Page::Friends||!f){
            sidebar();box(234,48,554,252);
            label(r,"与好友直接对话",256,70,490,28,accent);
            label(r,"添加 Tox ID，或接受收到的好友请求。\n使用实体键盘输入，回车发送文字。\n离线消息先保存，上线后自动发送。",256,117,490,95);
            label(r,"好友请求 "+std::to_string(view.requests.size())+" · 待发送 "+std::to_string(view.queued),256,220,240,25,muted);button("扫码添加",594,243,172,nav_scan);
            footer="W/S 选好友  ·  回车聊天  ·  I 我的 ID  ·  电源返回菜单";
        }else{
            box(12,48,776,252);label(r,f->name+(f->online?" · 在线":" · 离线"),28,59,392,25,accent);
            button("↑",438,52,43,LV_KEY_UP);button("↓",488,52,43,LV_KEY_DOWN);
            button("附件",541,52,71,nav_files);button("拍照",620,52,71,nav_photo);button("语音",699,52,77,nav_voice);
            // A bounded, scrollable LVGL history avoids allocating a widget for
            // every stored message. At most six messages are rendered at once.
            auto*history=box(24,88,752,154,surface);lv_obj_add_flag(history,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(history,LV_DIR_VER);
            size_t end=view.messages.size()>history_offset?view.messages.size()-history_offset:0;
            size_t begin=end>6?end-6:0;int y=4;
            if(!end)label(history,"还没有消息，输入第一句话吧。",10,40,726,50,muted);
            for(size_t i=begin;i<end;i++)y=chat_bubble(history,view.messages[i],i,y,752);
            lv_obj_update_layout(history);chat_history=history;
            if(rendered_offset!=history_offset)chat_scroll=-1;rendered_offset=history_offset;
            lv_obj_scroll_to_y(history,chat_scroll<0?lv_obj_get_scroll_bottom(history):chat_scroll,LV_ANIM_OFF);
            lv_obj_add_event_cb(history,[](lv_event_t*e){auto*o=static_cast<lv_obj_t*>(lv_event_get_target(e));chat_scroll=history_offset==0&&lv_obj_get_scroll_bottom(o)<=2?-1:lv_obj_get_scroll_y(o);},LV_EVENT_SCROLL,nullptr);
            box(24,251,649,39,raised);label(r,drafts[selected].empty()?"输入文字…":tail(drafts[selected],132)+"_",36,259,620,26,drafts[selected].empty()?muted:ink);button(pending?"保存中":"发送",684,252,92,LV_KEY_ENTER,true);
            footer="拍摄键中文 · Shift＋音量滚动 · 黄钟待发 / 灰勾已发 / 绿勾送达";
        }
    }else if(page==Page::Queue){
        box(12,48,776,252);auto rows=queue_items();if(queue_index>=rows.size())queue_index=rows.empty()?0:rows.size()-1;
        label(r,"待发送队列 · "+(friend_now()?friend_now()->name:std::string()),28,57,740,25,accent);
        if(queue_confirm){
            label(r,queue_confirm==2?"这条消息可能已被对方收到。\n确认重新发送吗？":"取消后不再自动发送这条消息。",30,108,734,90);
            button("确认",428,249,160,nav_queue_yes,true);button("返回",604,249,164,nav_queue_no);
        }else{
            size_t start=queue_index/4*4;int y=91;
            for(size_t i=start;i<rows.size()&&i<start+4;i++){auto&m=*rows[i];auto*b=button((text_status(m.state)+" · "+m.text).c_str(),27,y,746,nav_queue_row+uint32_t(i),i==queue_index);auto*l=lv_obj_get_child(b,0);lv_obj_set_pos(l,38,6);lv_obj_set_width(l,694);status_icon(b,m.state,11,8,i==queue_index);y+=37;}
            if(rows.empty())label(r,"没有待发送的消息。\n好友离线时发送的文字、照片和语音会保存在这里。",30,120,727,79,muted);
            else{
                auto&m=*rows[queue_index];button(m.state=="queued"?"取消待发":"不再重发",27,255,162,nav_cancel_queued);
                if(m.state!="queued")button("重新发送",202,255,162,nav_resend,true);
                label(r,std::to_string(queue_index+1)+" / "+std::to_string(rows.size()),580,261,68,24,muted);
            }
            button("返回聊天",657,255,116,screen::KEY_EXIT);
        }
        footer="W/S 选择 · 退格取消 · 待发送会上线自动投递；未确认需手动处理";
    }else if(page==Page::Photo||page==Page::Voice||page==Page::Attachment){
        box(12,48,776,252);box(24,61,490,225,0x090f13);
        if(media){
            if(!media->pixels.empty()){media_image=lv_image_create(r);lv_obj_set_pos(media_image,24+(490-media->width)/2,61+(225-media->height)/2);image_source(media_image,media_descriptor,media->pixels,media->width,media->height);}
            else label(r,media->recording?"●  正在录音\n"+std::to_string(media->seconds)+" / 30 秒":media->kind=="voice"?"语音消息\n点试听后可暂停":"等待相机画面…",92,127,380,83,media->recording?amber:ink);
            label(r,media->status,532,62,236,72,muted);
            if(media->camera)button("拍摄 / 空格",534,143,234,nav_capture,true);
            else if(media->recording)button("结束录音",534,143,234,nav_capture,true);
            else if(media->ready){
                if(media->kind=="voice")button(media->playing?"停止试听":"试听语音",534,139,234,nav_play);
                if(page!=Page::Attachment)button(pending?"正在发送":"确认发送",534,182,234,LV_KEY_ENTER,true);
            }
            if(page!=Page::Attachment&&!media->recording&&!media->camera)button("重新拍摄 / 录音",534,224,234,nav_retry);
            button("返回",534,265,234,screen::KEY_EXIT);
        }
        footer=page==Page::Attachment?(std::string(media&&media->kind=="voice"?"回车播放 / 暂停语音  ·  ":"")+(attachment_origin==Page::Chat?"返回聊天":"返回附件")):"拍好或录完后，按确认发送才会传给好友";
    }else if(page==Page::Files){
        box(12,48,776,252);label(r,"照片与语音 · W/S 选择，回车查看 / 接受",28,58,740,26,accent);
        size_t total=view.transfers.size();for(auto&m:view.messages)if(!m.file.empty())total++;
        if(total)file_index=std::min(file_index,total-1);else file_index=0;
        size_t start=file_index/4*4;int y=92;
        for(size_t i=start;i<total&&i<start+4;i++){
            std::string title;
            if(i<view.transfers.size()){auto&t=view.transfers[i];title=(t.mine?"发送 ":"接收 ")+t.name+" · "+(t.state=="offered"?"等待接受":t.state=="waiting"?"等待对方接受":std::to_string(t.done*100/t.size)+"%");}
            else{size_t index=i-view.transfers.size();for(auto it=view.messages.rbegin();it!=view.messages.rend();++it)if(!it->file.empty()){if(index--==0){title=(it->mine?"我：":"好友：")+it->text+" · "+transfer_status(it->state);break;}}}
            button(title.c_str(),27,y,746,nav_file_row+uint32_t(i),i==file_index);y+=43;
        }
        if(!total)label(r,"还没有附件。聊天页可以拍照、录音发送。",30,146,725,60,muted);
        footer="回车查看 / 接受  ·  退格拒绝待接收附件或取消传输  ·  返回聊天";
    }else if(page==Page::Profile){
        box(12,48,776,252);
        if(qr_id!=view.id){lv_image_cache_drop(&qr_descriptor);own_qr=chat::make_tox_qr(view.id);qr_id=view.id;}
        if(own_qr.size){auto*o=lv_image_create(r);lv_obj_set_pos(o,22+(234-own_qr.size)/2,57+(234-own_qr.size)/2);image_source(o,qr_descriptor,own_qr.pixels,own_qr.size,own_qr.size);}
        else label(r,"正在生成二维码…",35,144,216,55,muted);
        label(r,"我的 Tox ID",278,61,472,27,accent);
        std::string id;for(size_t i=0;i<view.id.size();i+=26)id+=view.id.substr(i,26)+"\n";
        label(r,id,278,103,485,82);
        label(r,"让好友扫描左侧二维码添加你。\n仅包含公开 ID，不包含身份私钥。",278,195,485,49,muted);
        button("N 修改昵称",278,255,148,'n');button("扫码添加",440,255,148,nav_scan);button("返回",659,255,113,screen::KEY_EXIT);
        footer="I 我的 ID  ·  实体拍摄键扫描对方  ·  返回键回到好友";
    }else if(page==Page::Scan){
        box(12,48,776,252);scan_image=lv_image_create(r);lv_obj_set_pos(scan_image,20,56);image_source(scan_image,scan_descriptor,scan_view.pixels,chat::ScanView::width,chat::ScanView::height);
        label(r,"扫描好友二维码",279,62,341,28,accent);button("取消",660,56,111,screen::KEY_EXIT);
        button("1×",279,103,85,nav_zoom1,scan_view.zoom==100);button("1.5×",375,103,85,nav_zoom15,scan_view.zoom==150);button("2×",471,103,85,nav_zoom2,scan_view.zoom==200);
        label(r,scan_view.capture_width>=1024?"高清识别":"",578,109,185,25,muted);
        button("F 自动对焦",279,151,166,'f');button("W 调焦 −",457,151,151,'w');button("S 调焦 ＋",619,151,151,'s');
        scan_focus=lv_label_create(r);lv_obj_set_pos(scan_focus,279,198);lv_obj_set_size(scan_focus,490,49);lv_obj_set_style_text_color(scan_focus,lv_color_hex(muted),0);lv_label_set_text(scan_focus,scan_view.focus_status.c_str());
        scan_status=lv_label_create(r);lv_obj_set_pos(scan_status,279,249);lv_obj_set_size(scan_status,490,48);lv_obj_set_style_text_color(scan_status,lv_color_hex(scan_view.failed?amber:accent),0);lv_label_set_text(scan_status,scan_view.status.c_str());
        footer="Z 缩放  ·  F / 拍摄键对焦  ·  W/S 微调  ·  返回取消";
    }else if(page==Page::Add||page==Page::Rename){
        box(12,48,776,252);label(r,page==Page::Add?(scanned?"确认扫描到的好友":"添加好友"):"修改昵称",30,63,680,26,accent);
        label(r,page==Page::Add?(scanned?"请核对对方 ID；按回车才发送好友请求。":"输入完整 76 位 Tox ID，或按实体拍摄键扫描。"):"输入昵称，确认后保存。",30,106,730,28,muted);
        box(26,151,746,84,raised);std::string shown=edit;
        if(page==Page::Add&&shown.size()>38)shown.insert(38,"\n");label(r,shown+"_",40,164,718,60);
        button(pending?"处理中":"回车确认",30,254,150,LV_KEY_ENTER,true);button("取消",661,254,111,screen::KEY_EXIT);if(page==Page::Add)button("扫码添加",194,254,149,nav_scan);
        footer=page==Page::Add?std::to_string(edit.size())+" / 76  ·  Shift + Q–P 输入数字  ·  退格删除":"拍摄键中文 · 双击 Shift 大写 · 退格删除 · 返回取消";
    }else if(page==Page::Requests){
        box(12,48,776,252);label(r,"好友请求 "+std::to_string(view.requests.size())+" · 待发送 "+std::to_string(view.queued),30,63,700,26,accent);
        if(view.requests.empty())label(r,"没有待处理请求。\n新请求会出现在这里，接受后才能聊天。",30,119,730,85,muted);
        else{request_index=std::min(request_index,view.requests.size()-1);auto&q=view.requests[request_index];label(r,q.key.substr(0,32)+"\n"+q.key.substr(32),30,102,730,57);label(r,q.message,30,170,730,62);button("回车接受",30,251,147,LV_KEY_ENTER,true);button("退格拒绝",189,251,147,LV_KEY_BACKSPACE);label(r,std::to_string(request_index+1)+" / "+std::to_string(view.requests.size()),664,257,96,28,muted);}
        footer="W/S 切换请求  ·  接受后加入好友  ·  返回键回到好友";
    }else{
        box(12,48,776,252);label(r,"删除好友？",30,68,730,30,amber);auto*f=friend_now();label(r,(f?f->name:"")+"\n将同时删除本机保存的对话记录。",30,124,724,91);button("确认删除",30,249,147,LV_KEY_ENTER);button("取消",191,249,113,screen::KEY_EXIT);footer="回车确认  ·  返回键取消";
    }
    bool show_error=!view.error.empty()&&(!view.ready||view.error_count!=dismissed_error);
    if(!notice.empty())footer=notice;else if(show_error)footer=view.error;
    label(r,footer,18,310,770,24,(!notice.empty()||show_error)?amber:muted);
}
void key(uint32_t k){
    if(text_input.key(k))return;
    dismissed_error=view.error_count;
    if(k==screen::KEY_HOME){screen::quit=true;return;}
    if(k==screen::KEY_SYMBOL&&!pending&&(page==Page::Chat||page==Page::Rename)){
        auto origin=page;auto number=selected;bool chat_page=page==Page::Chat;
        text_input.open("tox",chat_page?"聊天草稿":"我的昵称",chat_page?drafts[number]:edit,chat_page?1024:64,font,[origin,number](std::string value){
            if(origin==Page::Chat)drafts[number]=std::move(value);else if(page==origin)edit=std::move(value);notice="已填入草稿 · 回车发送或保存";paint();
        },chat_page,chat_page?1024:64);return;
    }
    if(k==screen::KEY_MODE)return;
    if(k==screen::KEY_FONT_UP||k==screen::KEY_FONT_DOWN){
        if(page==Page::Chat&&chat_history){bool up=k==screen::KEY_FONT_UP;
            if(up&&lv_obj_get_scroll_y(chat_history)>0)lv_obj_scroll_by(chat_history,0,100,LV_ANIM_OFF);
            else if(!up&&lv_obj_get_scroll_bottom(chat_history)>0)lv_obj_scroll_by(chat_history,0,-100,LV_ANIM_OFF);
            else{history_offset=up?std::min<unsigned>(view.messages.size()>0?view.messages.size()-1:0,history_offset+3):history_offset>3?history_offset-3:0;chat_scroll=-1;paint();}
        }return;
    }
    if(k==screen::KEY_EXIT){if(pending)return;if(page==Page::Photo||page==Page::Voice||page==Page::Attachment){bool attachment=page==Page::Attachment;stop_media();page=attachment?attachment_origin:Page::Chat;}else if(page==Page::Files||page==Page::Queue){if(page==Page::Queue&&queue_confirm)queue_confirm=0;else page=Page::Chat;}else if(page==Page::Scan){stop_scan();page=scan_origin;}else page=Page::Friends;notice.clear();paint();return;}
    if(pending)return;
    notice.clear();
    if(k==nav_background){submit({chat::Action::Background,0,view.background?"0":"1"});paint();return;}
    if(k==nav_queue&&(page==Page::Chat||page==Page::Queue)){page=Page::Queue;queue_confirm=0;queue_index=0;paint();return;}
    if(page==Page::Queue){
        auto rows=queue_items();if(queue_index>=rows.size())queue_index=rows.empty()?0:rows.size()-1;
        if(queue_confirm){
            if(k==nav_queue_no||k=='n')queue_confirm=0;
            else if(k==nav_queue_yes||k==LV_KEY_ENTER||k=='y'){submit({queue_confirm==2?chat::Action::RetryQueued:chat::Action::CancelQueued,selected,std::to_string(queue_target)});queue_confirm=0;}
        }else if(!rows.empty()){
            if(k>=nav_queue_row&&k<nav_queue_row+128)queue_index=std::min<size_t>(k-nav_queue_row,rows.size()-1);
            else if(k=='w'||k==LV_KEY_UP)queue_index=(queue_index+rows.size()-1)%rows.size();
            else if(k=='s'||k==LV_KEY_DOWN)queue_index=(queue_index+1)%rows.size();
            else if(k==nav_cancel_queued||k==LV_KEY_BACKSPACE){queue_target=rows[queue_index]->id;queue_confirm=1;}
            else if((k==nav_resend||k==LV_KEY_ENTER)&&rows[queue_index]->state!="queued"){queue_target=rows[queue_index]->id;queue_confirm=2;}
        }
        paint();return;
    }
    try {
        if(page==Page::Chat&&k>=nav_bubble&&k<nav_bubble+128){
            size_t index=k-nav_bubble;if(index<view.messages.size()){auto&m=view.messages[index];if(m.file.empty())return;
                if(m.kind=="voice"){if(!inline_audio||inline_audio->file!=m.file){inline_audio=std::make_unique<chat::Media>(c1::data()+"/tox/media");inline_audio->load(m.file,m.kind);}inline_audio->play();}
                else{stop_media();media=std::make_unique<chat::Media>(c1::data()+"/tox/media");media->load(m.file,m.kind);attachment_origin=Page::Chat;page=Page::Attachment;}
            }paint();return;
        }
        if((k==nav_photo||k==nav_voice)&&friend_now()){
            stop_scan();stop_media();media=std::make_unique<chat::Media>(c1::data()+"/tox/media");
            if(k==nav_voice){page=Page::Voice;media->record();}else{page=Page::Photo;media->open_camera();}paint();return;
        }
        if(k==nav_files&&page==Page::Chat){page=Page::Files;file_index=0;paint();return;}
        if(page==Page::Photo||page==Page::Voice||page==Page::Attachment){
            if(!media)return;
            if(k==nav_retry){if(page==Page::Photo)media->open_camera();else media->record();}
            else if(k==nav_play||(page==Page::Attachment&&k==LV_KEY_ENTER))media->play();
            else if(k==nav_capture||k==screen::KEY_SYMBOL||k==' '||k==LV_KEY_ENTER){
                if(media->camera)media->capture();else if(media->recording)media->finish_record();
                else if(media->ready&&page!=Page::Attachment&&k==LV_KEY_ENTER){media->keep();submit({chat::Action::SendFile,selected,media->file});}
            }paint();return;
        }
        if(page==Page::Files){
            size_t total=view.transfers.size();for(auto&m:view.messages)if(!m.file.empty())total++;
            if(k>=nav_file_row&&k<nav_file_row+128){file_index=k-nav_file_row;k=LV_KEY_ENTER;}
            if(total){file_index=std::min(file_index,total-1);
                if(k=='w'||k=='W'||k==LV_KEY_UP)file_index=(file_index+total-1)%total;
                else if(k=='s'||k=='S'||k==LV_KEY_DOWN)file_index=(file_index+1)%total;
                else if(file_index<view.transfers.size()){auto&t=view.transfers[file_index];
                    if(k==LV_KEY_BACKSPACE)submit({chat::Action::CancelFile,selected,std::to_string(t.id)});
                    else if(k==LV_KEY_ENTER&&!t.mine&&t.state=="offered")submit({chat::Action::AcceptFile,selected,std::to_string(t.id)});
                }else if(k==LV_KEY_ENTER){size_t index=file_index-view.transfers.size();for(auto it=view.messages.rbegin();it!=view.messages.rend();++it)if(!it->file.empty()&&index--==0){stop_media();media=std::make_unique<chat::Media>(c1::data()+"/tox/media");media->load(it->file,it->kind);attachment_origin=Page::Files;page=Page::Attachment;break;}}
            }paint();return;
        }
    }catch(const std::exception&e){notice=e.what();paint();return;}
    if(k==nav_scan||(k==screen::KEY_SYMBOL&&(page==Page::Friends||page==Page::Add||page==Page::Profile))){start_scan();return;}
    if(page==Page::Scan&&scanner){
        if(k==nav_zoom1||k==nav_zoom15||k==nav_zoom2||k=='z'||k=='Z'){
            unsigned zoom=k==nav_zoom1?100:k==nav_zoom15?150:k==nav_zoom2?200:scan_view.zoom==100?150:scan_view.zoom==150?200:100;
            scanner->set_zoom(zoom);scan_view.zoom=zoom;
        }else if(k=='f'||k=='F'||k==screen::KEY_SYMBOL)scanner->autofocus();
        else if(k=='w'||k=='W')scanner->adjust_focus(-1);
        else if(k=='s'||k=='S')scanner->adjust_focus(1);
        else if(k!=nav_profile&&k!=nav_add&&k!=nav_requests)return;
        if(k!=nav_profile&&k!=nav_add&&k!=nav_requests){paint();return;}
    }
    if(k==screen::KEY_SYMBOL)return;
    if(k==nav_profile||k==nav_add||k==nav_requests ){stop_scan();page=k==nav_profile?Page::Profile:k==nav_add?Page::Add:Page::Requests;edit.clear();scanned=false;paint();return;}
    if(page==Page::Scan)return;
    if(page==Page::Add||page==Page::Rename){
        if(k==LV_KEY_BACKSPACE){erase_utf8(edit);scanned=false;}
        else if(k==LV_KEY_ENTER){
            if(page==Page::Add){std::string id,error;if(chat::parse_tox_qr(edit,id,error,view.id)){edit=id;submit({chat::Action::Add,0,edit});}else notice=error;}
            else submit({chat::Action::Rename,0,edit});
        }
        else if(k>=32&&k<127){if(page==Page::Rename&&edit.size()<64)edit+=char(k);else if(page==Page::Add&&edit.size()<76&&((k>='0'&&k<='9')||(k>='a'&&k<='f')||(k>='A'&&k<='F')))edit+=char(k);}
    }else if(page==Page::Chat){
        if(k==LV_KEY_BACKSPACE)erase_utf8(drafts[selected]);
        else if(k==LV_KEY_ENTER){if(!drafts[selected].empty())submit({chat::Action::Send,selected,drafts[selected]});}
        else if(k==LV_KEY_UP){history_offset=std::min<unsigned>(view.messages.size()>0?view.messages.size()-1:0,history_offset+3);chat_scroll=-1;}
        else if(k==LV_KEY_DOWN){history_offset=history_offset>3?history_offset-3:0;chat_scroll=-1;}
        else if(k>=32&&k<127&&drafts[selected].size()<1024)drafts[selected]+=char(k);
    }else if(page==Page::Delete){if(k==LV_KEY_ENTER)submit({chat::Action::Delete,selected});}
    else if(page==Page::Profile){if(k=='n'||k=='N'){page=Page::Rename;edit=view.name;}}
    else if(page==Page::Requests){
        if(!view.requests.empty()){
            if(k=='w'||k=='W'||k==LV_KEY_UP)request_index=(request_index+view.requests.size()-1)%view.requests.size();
            else if(k=='s'||k=='S'||k==LV_KEY_DOWN)request_index=(request_index+1)%view.requests.size();
            else if(k==LV_KEY_ENTER||k==LV_KEY_BACKSPACE)submit({k==LV_KEY_ENTER?chat::Action::Accept:chat::Action::Reject,0,view.requests[request_index].key});
        }
    }else{
        if(k=='a'||k=='A'){page=Page::Add;edit.clear();scanned=false;}
        else if(k=='i'||k=='I')page=Page::Profile;
        else if(k=='r'||k=='R')page=Page::Requests;
        else if(!view.friends.empty()){
            auto it=std::find_if(view.friends.begin(),view.friends.end(),[](const chat::Friend&f){return f.number==selected;});size_t i=it==view.friends.end()?0:it-view.friends.begin();
            if(k=='w'||k=='W'||k==LV_KEY_UP)i=(i+view.friends.size()-1)%view.friends.size();
            if(k=='s'||k=='S'||k==LV_KEY_DOWN)i=(i+1)%view.friends.size();selected=view.friends[i].number;
            if(k==LV_KEY_ENTER){open_chat(selected);return;}
            if(k==LV_KEY_BACKSPACE)page=Page::Delete;
        }
    }paint();
}
}
int main(int argc,char**argv){
    if(argc==3&&std::string(argv[1])=="--stop-service")return chat::stop_service(argv[2])?0:1;
    if(argc==4&&std::string(argv[1])=="--service")return chat::run_service(argv[2],argv[3]);
    signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);
    if(!screen::open())return 1;
    font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),18);
    std::string nodes=c1::root()+"/tox/bootstrap.json";if(access((c1::data()+"/tox/bootstrap.json").c_str(),R_OK)==0)nodes=c1::data()+"/tox/bootstrap.json";
    engine=std::make_unique<chat::Session>(c1::data()+"/tox",nodes,"/proc/self/exe");paint();uint32_t last=0;
    while(!stopped&&!screen::quit){
        lv_timer_handler();for(uint32_t k;(k=screen::take_key());)key(k);
        if(screen::tick()-last>=100){last=screen::tick();auto next=engine->snapshot(selected);bool dirty=next.revision!=view.revision;view=std::move(next);
            if(selected==UINT32_MAX&&!view.friends.empty())selected=view.friends.front().number;
            if(pending&&view.completed==pending){pending=0;dirty=true;if(view.command_ok){
                if(pending_action==chat::Action::Send){drafts[selected].clear();history_offset=0;chat_scroll=-1;notice=view.background?"已保存，上线后自动发送":"已保存；离开应用后继续补发需开启后台";}
                else if(pending_action==chat::Action::SendFile){stop_media();page=Page::Files;file_index=0;notice="附件已保存；上线后自动发出请求，仍需对方接受";}
                else if(pending_action==chat::Action::Background){notice=view.background?"已开启后台：退出后继续收信；重启后需打开一次 Tox":"已关闭后台：退出后停止收信";}
                else if(pending_action==chat::Action::CancelQueued)notice="已取消自动发送，聊天记录保留";
                else if(pending_action==chat::Action::RetryQueued)notice="已重新加入待发队列";
                else if(pending_action==chat::Action::Rename)page=Page::Profile;
                else if(pending_action==chat::Action::Add){page=Page::Friends;notice="好友请求已提交，等待对方接受";}
                else if(pending_action==chat::Action::Delete){drafts.erase(selected);selected=UINT32_MAX;page=Page::Friends;}
            }}
            if(page==Page::Scan&&scanner){
                chat::ScanView next_scan;next_scan.revision=scan_view.revision;
                if(scanner->snapshot(next_scan)){
                    // Invalidate before replacing backing pixels; all LVGL access stays here.
                    if(next_scan.zoom!=scan_view.zoom||next_scan.capture_width!=scan_view.capture_width)dirty=true;
                    lv_image_cache_drop(&scan_descriptor);if(next_scan.pixels.empty())next_scan.pixels=std::move(scan_view.pixels);scan_view=std::move(next_scan);
                    if(!scan_view.id.empty()){
                        std::string id=scan_view.id;stop_scan();
                        bool exists=false;for(auto&f:view.friends)if(f.key==id.substr(0,64))exists=true;
                        page=Page::Add;edit=id;scanned=true;notice=exists?"对方已在好友列表中":"已识别，请核对 ID 后按回车确认";dirty=true;
                    }else if(scan_image&&!scan_view.pixels.empty()){
                        image_source(scan_image,scan_descriptor,scan_view.pixels,chat::ScanView::width,chat::ScanView::height);
                        lv_label_set_text(scan_focus,scan_view.focus_status.c_str());
                        lv_label_set_text(scan_status,scan_view.status.c_str());lv_obj_set_style_text_color(scan_status,lv_color_hex(scan_view.failed?amber:accent),0);
                    }else dirty=true;
                }
            }
            if(media){try{auto old_seconds=media->seconds;bool was_recording=media->recording,was_ready=media->ready;media->poll();
                if(was_recording!=media->recording||old_seconds!=media->seconds||was_ready!=media->ready)dirty=true;
                if(media->camera&&!media->pixels.empty()){if(media_image)image_source(media_image,media_descriptor,media->pixels,media->width,media->height);else dirty=true;}
            }catch(const std::exception&e){notice=e.what();dirty=true;}}
            if(inline_audio){try{bool was=inline_audio->playing;inline_audio->poll();if(was!=inline_audio->playing)dirty=true;}catch(const std::exception&e){notice=e.what();inline_audio.reset();dirty=true;}}
            if(dirty)paint();
        }usleep(10000);
    }
    if(scroll_shortcuts)unlink("/tmp/c1max-tox-scroll.pid");inline_audio.reset();stop_scan();stop_media();engine.reset();lv_image_cache_drop(&qr_descriptor);lv_image_cache_drop(&scan_descriptor);text_input.close();lv_obj_clean(lv_screen_active());for(auto&[file,p]:previews)lv_image_cache_drop(&p.image);previews.clear();lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);if(font)lv_tiny_ttf_destroy(font);screen::close();return 0;
}
