#include "client.hpp"
#include "segment_stream.hpp"
#include "subtitle_stream.hpp"
#include "timeline.hpp"
#include "y4m.hpp"
#include "frame_watchdog.hpp"
#include <sys/stat.h>
#include "display.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <atomic>
#include <future>
#include <functional>
#include <memory>
#include <cmath>
#include <tinyalsa/mixer.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <fcntl.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
namespace fs=std::filesystem;
static MediaClient client;
static lv_obj_t *content=nullptr,*status_label=nullptr,*server=nullptr,*username=nullptr,*password=nullptr,*kind=nullptr;
static lv_font_t *font=nullptr,*small_font=nullptr,*large_font=nullptr;
static lv_color_t accent(){return lv_color_hex(client.config.value("type",std::string())=="Jellyfin"?0x00a4dc:0x52b54b);}
static Json libraries=Json::array();
static size_t library_index=0,item_index=0;
static int total_items=0;
static lv_obj_t *browse_area=nullptr;
static std::vector<lv_obj_t*> subtitle_buttons;
static size_t subtitle_index=0;
static lv_obj_t* active_field=nullptr;
static std::vector<lv_obj_t*> physical_buttons;
static size_t physical_index=0;

static std::future<Json> job;
static std::function<void(Json)> completed;
static bool busy=false;
static std::string parent_id,title="StreamPlayer",mode="stream";
static int page=0;
static Json rows;
static pid_t player=-1;
static int player_in=-1,player_out=-1,video_pipe=-1;
static pid_t player_group=-1;
static std::string video_fifo,frame_error;
static bool decoder_pipe=false;
static Y4mReader video_reader;
static FrameWatchdog frame_watchdog;
enum class View { Setup, Libraries, Items, Detail, Updates, Roms };
static View view=View::Setup;
static Playback current;
static StreamOptions requested_options,displayed_options;
static std::unique_ptr<SegmentStream> segments;
static SegmentStream::State segment_state;
static std::string segment_fifo,stream_notice;
static std::vector<Playback> retired_sessions;
static bool segment_clock=false,segment_timeline=false;
static int frame_width=0,frame_height=0;
static int64_t applied_change=-1;
static lv_obj_t *subtitle_label=nullptr,*subtitle_panel=nullptr;
static std::unique_ptr<SubtitleStream> local_subtitles;
static int subtitle_track=-1,subtitle_size=32,rendered_subtitle_size=0;
static lv_font_t* subtitle_font=nullptr;
static std::shared_ptr<const subtitles::Window> caption_window;
static const subtitles::Cue* caption_cue=nullptr;
static std::string caption_text,subtitle_error,subtitle_render_error;
static bool subtitle_loading=false;
static void close_subtitles();
static void show_subtitles();
static void poll_subtitles();
static std::string player_buffer;
static uint32_t player_start=0,last_query=0,last_report=0,last_frame_report=0;
static int64_t position=0;
static bool reported=false,ended=false,playback_failed=false,server_session_dirty=false;
static bool paused=false,pause_on_start=false,have_time=false,restarting=false,width_fill=true;
static bool controls_visible=true,dragging=false,report_changed=false,seek_cancelled=false;
static int64_t seek_target=-1;
static PlaybackTimeline timeline;
static uint32_t controls_touched=0,last_controls=0;
static lv_obj_t *video_root=nullptr,*video_top=nullptr,*video_bar=nullptr,*progress=nullptr;
static lv_obj_t *time_label=nullptr,*play_label=nullptr,*volume_label=nullptr,*fit_label=nullptr;
static struct mixer* audio_mixer=nullptr;
static int last_volume=-1,unmute_volume=50;
static volatile sig_atomic_t interrupted=0;
static void sig(int){interrupted=1;}
static uint64_t received_frames(){return video_reader.frames();}
static lv_obj_t* label(lv_obj_t*p,const std::string&s,int x,int y,int w){auto o=lv_label_create(p);lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);return o;}
static lv_obj_t* button(lv_obj_t*p,const std::string&s,int x,int y,int w,lv_event_cb_t cb,void*arg=nullptr){auto o=lv_button_create(p);physical_buttons.push_back(o);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,lv_color_hex(0x101b25),0);lv_obj_set_style_text_color(o,lv_color_hex(0xffffff),0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,40);auto l=lv_label_create(o);lv_label_set_text(l,s.c_str());lv_obj_set_width(l,w-16);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(l);lv_obj_set_style_radius(o,8,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,lv_color_hex(0x243440),0);lv_obj_add_event_cb(o,cb,LV_EVENT_CLICKED,arg);return o;}
static void status(const std::string&s){if(status_label)lv_label_set_text(status_label,s.c_str());}
static void screen_base(const std::string &heading){
    active_field=nullptr;physical_buttons.clear();physical_index=0;browse_area=nullptr;
    auto s=lv_screen_active();lv_obj_clean(s);lv_obj_remove_flag(s,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s,lv_color_hex(0x04070b),0);lv_obj_set_style_text_color(s,lv_color_hex(0xeaf1f8),0);if(font)lv_obj_set_style_text_font(s,font,0);
    auto dot=lv_obj_create(s);lv_obj_remove_style_all(dot);lv_obj_set_pos(dot,18,18);lv_obj_set_size(dot,12,12);lv_obj_set_style_radius(dot,6,0);lv_obj_set_style_bg_color(dot,accent(),0);lv_obj_set_style_bg_opa(dot,LV_OPA_COVER,0);
    auto h=label(s,heading,42,8,735);lv_label_set_long_mode(h,LV_LABEL_LONG_DOT);if(large_font)lv_obj_set_style_text_font(h,large_font,0);
    content=lv_obj_create(s);lv_obj_set_pos(content,10,46);lv_obj_set_size(content,780,252);lv_obj_set_style_bg_color(content,lv_color_hex(0x080e14),0);lv_obj_set_style_bg_opa(content,LV_OPA_COVER,0);lv_obj_set_style_text_color(content,lv_color_hex(0xeaf1f8),0);lv_obj_set_style_border_width(content,1,0);lv_obj_set_style_border_color(content,lv_color_hex(0x172431),0);lv_obj_set_style_radius(content,12,0);lv_obj_set_style_pad_all(content,0,0);
    status_label=label(s,"",18,308,764);if(small_font)lv_obj_set_style_text_font(status_label,small_font,0);lv_obj_set_style_text_color(status_label,lv_color_hex(0x8b98a8),0);lv_label_set_long_mode(status_label,LV_LABEL_LONG_DOT);
}
static void work(const std::string&message,std::function<Json()>run,std::function<void(Json)>done){if(busy)return;busy=true;status(message);if(content)lv_obj_add_state(content,LV_STATE_DISABLED);completed=std::move(done);job=std::async(std::launch::async,[run]{try{return Json{{"ok",true},{"result",run()}};}catch(std::exception&e){return Json{{"ok",false},{"error",e.what()}};}});}
void show_setup();void show_libraries();static void show_items();static void render_items();static void show_detail();static void start_player(const Playback&);
static void controls(bool);static void update_controls();static void toggle_pause();
static void seek_by(int);static void seek_to(int64_t);static void toggle_fit();
static void volume_change(int);static void toggle_mute();
static lv_obj_t* field(const char*placeholder,const std::string&value,int y,bool secret=false){
    auto o=lv_textarea_create(content);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_bg_color(o,lv_color_hex(0x0a1119),0);lv_obj_set_style_text_color(o,lv_color_hex(0xffffff),0);
    lv_obj_set_pos(o,8,y);lv_obj_set_size(o,500,42);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,lv_color_hex(0x243440),0);lv_obj_set_style_border_color(o,accent(),LV_STATE_FOCUSED);lv_obj_set_style_radius(o,7,0);lv_textarea_set_one_line(o,true);lv_textarea_set_placeholder_text(o,placeholder);lv_textarea_set_text(o,value.c_str());lv_textarea_set_password_mode(o,secret);
    lv_obj_add_event_cb(o,[](lv_event_t*e){if(!busy){auto target=(lv_obj_t*)lv_event_get_target(e);if(active_field&&active_field!=target)lv_obj_remove_state(active_field,LV_STATE_FOCUSED);active_field=target;lv_obj_add_state(active_field,LV_STATE_FOCUSED);status(screen::caps_lock()?"ABC | Double Shift: abc | Shift: symbols | Enter: next":"abc | Double Shift: ABC | Shift: symbols | Enter: next");}},LV_EVENT_CLICKED,nullptr);return o;
}
void show_setup(){
    view=View::Setup;screen_base("StreamPlayer");
    label(content,"Server",20,16,80);
    kind=lv_dropdown_create(content);lv_obj_set_pos(kind,105,8);lv_obj_set_size(kind,235,40);lv_dropdown_set_options(kind,"Emby\nJellyfin");lv_dropdown_set_selected(kind,client.config.value("type",std::string())=="Jellyfin"?1:0);
    lv_obj_set_style_bg_color(kind,lv_color_hex(0x0a1119),0);lv_obj_set_style_text_color(kind,lv_color_hex(0xeaf1f8),0);lv_obj_set_style_border_color(kind,lv_color_hex(0x243440),0);
    label(content,"Jellyfin · Emby",536,16,220);
    auto line=lv_obj_create(content);lv_obj_remove_style_all(line);lv_obj_set_pos(line,20,56);lv_obj_set_size(line,738,2);lv_obj_set_style_bg_color(line,accent(),0);lv_obj_set_style_bg_opa(line,LV_OPA_COVER,0);
    label(content,"URL",20,78,68);server=field("http://server:8096",client.config.value("base",std::string()),68);lv_obj_set_pos(server,90,68);lv_obj_set_width(server,668);
    label(content,"User",20,131,68);username=field("Username",client.config.value("username",std::string()),122);lv_obj_set_pos(username,90,122);lv_obj_set_width(username,278);
    label(content,"Pass",390,131,68);password=field("Password",client.config.value("password",std::string()),122,true);lv_obj_set_pos(password,460,122);lv_obj_set_width(password,298);
    auto login=button(content,"Login",20,188,345,[](lv_event_t*){
        if(busy)return;std::string b=lv_textarea_get_text(server),u=lv_textarea_get_text(username),p=lv_textarea_get_text(password),t=lv_dropdown_get_selected(kind)?"Jellyfin":"Emby";
        lv_textarea_set_text(password,"");active_field=nullptr;
        work("Connecting...",[b,u,p,t]{client.login(b,u,p,t);return client.libraries();},[](Json j){libraries=j;show_libraries();});
    });lv_obj_set_style_bg_color(login,accent(),0);lv_obj_set_style_text_color(login,lv_color_hex(0x041008),0);
    if(client.ready())button(content,"Media library",398,188,360,[](lv_event_t*){show_libraries();});
    status("Enter: next field / connect   ·   Double Shift: ABC   ·   Return: back");
}
void show_libraries(){
    if(busy)return;view=View::Libraries;screen_base("StreamPlayer / Media library");
    work("Loading libraries...",[]{return client.libraries();},[](Json j){libraries=j;if(libraries.empty()){status("No libraries available");return;}library_index=std::min(library_index,libraries.size()-1);parent_id=libraries[library_index].at("Id");page=0;show_items();});
}
static void render_items(){
    view=View::Items;screen_base("StreamPlayer / "+(libraries.empty()?std::string("Videos"):libraries[library_index].value("Name",std::string("Videos"))));
    for(size_t i=0;i<libraries.size();++i){
        auto b=button(content,libraries[i].value("Name",std::string("Library")),10,8+int(i)*38,120,[](lv_event_t*e){if(busy)return;library_index=(size_t)lv_event_get_user_data(e);parent_id=libraries[library_index].at("Id");page=0;show_items();},(void*)i);
        lv_obj_set_height(b,34);if(i==library_index){lv_obj_set_style_border_color(b,accent(),0);lv_obj_set_style_text_color(b,accent(),0);}
    }
    physical_buttons.clear();physical_index=0;
    browse_area=lv_obj_create(content);lv_obj_remove_style_all(browse_area);lv_obj_set_pos(browse_area,144,6);lv_obj_set_size(browse_area,622,238);lv_obj_remove_flag(browse_area,LV_OBJ_FLAG_SCROLLABLE);
    for(size_t i=0;i<rows.size();++i){
        auto card=button(browse_area,"",int(i)*208,0,198,[](lv_event_t*e){item_index=(size_t)lv_event_get_user_data(e);show_detail();},(void*)i);
        lv_obj_set_height(card,196);lv_obj_set_style_pad_all(card,0,0);lv_obj_set_style_bg_color(card,lv_color_hex(0x0a1119),0);if(i==item_index)lv_obj_set_style_border_color(card,accent(),0);
        auto placeholder=label(card,LV_SYMBOL_VIDEO,12,57,170);lv_obj_set_style_text_align(placeholder,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(placeholder,accent(),0);
        auto path=rows[i].value("Poster",std::string());if(!path.empty()){
            auto image=lv_image_create(card);lv_image_set_src(image,("A:"+path).c_str());lv_obj_set_pos(image,28,4);
            lv_obj_set_size(image,140,140);lv_image_set_inner_align(image,LV_IMAGE_ALIGN_CONTAIN);
        }
        auto name=label(card,rows[i].value("Name",std::string("Video")),10,146,176);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        std::string meta=rows[i].contains("ProductionYear")?std::to_string(rows[i]["ProductionYear"].get<int>()):rows[i].value("SeriesName",std::string());
        auto info=label(card,meta,10,174,176);if(small_font)lv_obj_set_style_text_font(info,small_font,0);lv_label_set_long_mode(info,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(info,lv_color_hex(0x8b98a8),0);
    }
    if(page>0)button(browse_area,"Previous",0,202,158,[](lv_event_t*){--page;show_items();});
    auto num=label(browse_area,std::to_string(page+1)+" / "+std::to_string(std::max(1,(total_items+2)/3)),240,210,140);lv_obj_set_style_text_align(num,LV_TEXT_ALIGN_CENTER,0);
    if((page+1)*3<total_items)button(browse_area,"Next",462,202,158,[](lv_event_t*){++page;show_items();});
    status(rows.empty()?"No videos in this library":"A/D: page   ·   W/S: library   ·   J/K: select   ·   Enter: details");
}
static void show_items(){
    if(busy)return;view=View::Items;screen_base("StreamPlayer / Loading");
    auto p=parent_id;int offset=page*3;work("Loading videos & covers...",[p,offset]{
        auto j=client.items(p,offset);auto &items=j["Items"];
        for(auto&item:items){try{item["Poster"]=client.poster(item.at("Id"));}catch(...){}}
        return j;
    },[](Json j){rows=j.at("Items");total_items=j.value("TotalRecordCount",0);item_index=0;render_items();});
}
static void show_detail(){
    if(busy||item_index>=rows.size())return;view=View::Detail;auto item=rows[item_index];
    title=item.value("Name",std::string("Video"));screen_base("StreamPlayer / "+title);
    auto path=item.value("Poster",std::string());if(!path.empty()){auto im=lv_image_create(content);lv_image_set_src(im,("A:"+path).c_str());lv_obj_set_pos(im,14,12);lv_obj_set_size(im,140,194);lv_image_set_inner_align(im,LV_IMAGE_ALIGN_CONTAIN);}
    auto heading=label(content,title,176,10,576);if(large_font)lv_obj_set_style_text_font(heading,large_font,0);lv_label_set_long_mode(heading,LV_LABEL_LONG_DOT);
    auto overview=label(content,item.value("Overview",std::string("")),176,50,576);lv_obj_set_height(overview,112);if(small_font)lv_obj_set_style_text_font(overview,small_font,0);lv_label_set_long_mode(overview,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(overview,lv_color_hex(0xa6b5c4),0);
    auto play=button(content,"Play",176,190,272,[](lv_event_t*){
        if(busy)return;auto id=rows[item_index].at("Id").get<std::string>();auto next=std::make_shared<Playback>();auto old=current;bool dirty=server_session_dirty;auto options=requested_options;options.subtitle=-1;
        work("Preparing playback...",[id,next,old,dirty,options]{if(dirty)client.stop_transcode(old);*next=client.playback(id,0,options);return Json::object();},[next](Json){current=*next;server_session_dirty=true;pause_on_start=false;start_player(current);});
    });lv_obj_set_style_bg_color(play,accent(),0);lv_obj_set_style_text_color(play,lv_color_hex(0x041008),0);
    button(content,"Back",480,190,272,[](lv_event_t*){render_items();});
    status("During playback: F width/fit   ·   S subtitles   ·   Enter pause   ·   Return stop");
}
static void command(const char*s){if(player_in>=0) {auto unused=write(player_in,s,strlen(s));(void)unused;}}
static int volume_get(){
    if(!audio_mixer)audio_mixer=mixer_open(0);
    auto ctl=audio_mixer?mixer_get_ctl_by_name(audio_mixer,"softvolume"):nullptr;
    long v[2];if(!ctl||mixer_ctl_get_array(ctl,v,2))return -1;
    int lo=mixer_ctl_get_range_min(ctl),hi=mixer_ctl_get_range_max(ctl);
    return hi>lo?std::clamp<int>((v[0]-lo)*100/(hi-lo),0,100):-1;
}
static void volume_set(int value){
    auto ctl=audio_mixer?mixer_get_ctl_by_name(audio_mixer,"softvolume"):nullptr;if(!ctl)return;
    int lo=mixer_ctl_get_range_min(ctl),hi=mixer_ctl_get_range_max(ctl);value=std::clamp(value,0,100);
    long v[2]={lo+(hi-lo)*value/100,lo+(hi-lo)*value/100};
    mixer_ctl_set_array(ctl,v,2);controls(true);update_controls();
}
static void volume_change(int delta){int value=volume_get();if(value>=0)volume_set(value+delta);}
static void toggle_mute(){int v=volume_get();if(v<0)return;if(v){unmute_volume=v;volume_set(0);}else volume_set(unmute_volume);}
static std::string clock_text(int64_t ticks){
    auto t=std::max<int64_t>(0,ticks/10000000);char b[40];
    if(t>=3600)snprintf(b,sizeof b,"%lld:%02lld:%02lld",(long long)t/3600,(long long)t/60%60,(long long)t%60);
    else snprintf(b,sizeof b,"%02lld:%02lld",(long long)t/60,(long long)t%60);return b;
}
static void controls(bool visible){
    if(!video_root)return;if(subtitle_panel)visible=true;controls_visible=visible;controls_touched=screen::tick();
    for(auto o:{video_top,video_bar})if(visible)lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);
    screen::video_controls(visible,subtitle_panel!=nullptr);if(visible){lv_obj_invalidate(video_top);lv_obj_invalidate(video_bar);}
}
static lv_obj_t* video_button(lv_obj_t*parent,const char*text,int x,int y,int width,lv_event_cb_t callback,void*arg=nullptr){
    auto o=lv_button_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,40);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x101b25),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(o,0,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,5,0);
    auto l=lv_label_create(o);lv_label_set_text(l,text);lv_obj_center(l);
    lv_obj_add_event_cb(o,[](lv_event_t*){controls_touched=screen::tick();},LV_EVENT_PRESSED,nullptr);
    lv_obj_add_event_cb(o,callback,LV_EVENT_CLICKED,arg);return l;
}
static void create_controls(){
    if(video_root)return;
    video_root=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(video_root);
    lv_obj_set_size(video_root,800,340);lv_obj_set_pos(video_root,0,0);
    lv_obj_remove_flag(video_root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_add_flag(video_root,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(video_root,[](lv_event_t*){controls(!controls_visible);},LV_EVENT_CLICKED,nullptr);
    auto panel=[](lv_obj_t*parent,int y,int height){auto p=lv_obj_create(parent);lv_obj_remove_style_all(p);lv_obj_set_pos(p,0,y);lv_obj_set_size(p,800,height);lv_obj_set_style_bg_color(p,lv_color_hex(0x080e14),0);lv_obj_set_style_bg_opa(p,LV_OPA_COVER,0);lv_obj_remove_flag(p,LV_OBJ_FLAG_SCROLLABLE);return p;};
    video_top=panel(video_root,0,36);video_bar=panel(video_root,218,122);
    auto t=label(video_top,title,12,4,674);lv_label_set_long_mode(t,LV_LABEL_LONG_DOT);
    auto hide=video_button(video_top,"Hide",704,0,88,[](lv_event_t*){controls(false);});lv_obj_set_height(lv_obj_get_parent(hide),34);
    progress=lv_slider_create(video_bar);lv_obj_set_pos(progress,24,18);lv_obj_set_size(progress,752,12);
    lv_slider_set_range(progress,0,1000);lv_obj_set_ext_click_area(progress,12);
    lv_obj_set_style_bg_color(progress,lv_color_hex(0x344963),LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress,accent(),LV_PART_INDICATOR);
    lv_obj_add_event_cb(progress,[](lv_event_t*e){
        auto code=lv_event_get_code(e);
        if(code==LV_EVENT_PRESSED||code==LV_EVENT_PRESSING||code==LV_EVENT_RELEASED)controls_touched=screen::tick();
        if(code==LV_EVENT_PRESSED)dragging=true;
        if(code==LV_EVENT_VALUE_CHANGED)update_controls();
        if(code==LV_EVENT_RELEASED){dragging=false;seek_to(current.duration*lv_slider_get_value(progress)/1000);}
        if(code==LV_EVENT_PRESS_LOST)dragging=false;
    },LV_EVENT_ALL,nullptr);
    time_label=label(video_bar,"",18,39,250);volume_label=label(video_bar,"",276,39,506);
    video_button(video_bar,"-10s",12,76,78,[](lv_event_t*){seek_by(-10);});
    play_label=video_button(video_bar,"Pause",98,76,94,[](lv_event_t*){toggle_pause();});
    video_button(video_bar,"+10s",200,76,78,[](lv_event_t*){seek_by(10);});
    video_button(video_bar,"Mute",286,76,74,[](lv_event_t*){toggle_mute();});
    video_button(video_bar,"−",368,76,48,[](lv_event_t*){volume_change(-5);});
    video_button(video_bar,"+",424,76,48,[](lv_event_t*){volume_change(5);});
    fit_label=video_button(video_bar,"Width",480,76,100,[](lv_event_t*){toggle_fit();});
    subtitle_label=video_button(video_bar,"Sub",588,76,98,[](lv_event_t*){show_subtitles();});
    video_button(video_bar,"Stop",694,76,94,[](lv_event_t*){screen::tap=true;});
    last_volume=volume_get();controls(true);update_controls();
}
static void update_controls(){
    if(!video_root)return;
    int64_t displayed=dragging?current.duration*lv_slider_get_value(progress)/1000:(seek_target>=0?seek_target:position);
    auto time=clock_text(displayed)+" / "+(current.duration>0?clock_text(current.duration):"--:--");
    lv_label_set_text(time_label,time.c_str());
    if(!dragging&&current.duration>0)lv_slider_set_value(progress,std::clamp<int64_t>(displayed*1000/current.duration,0,1000),LV_ANIM_OFF);
    if(current.duration>0&&!restarting)lv_obj_remove_state(progress,LV_STATE_DISABLED);else lv_obj_add_state(progress,LV_STATE_DISABLED);
    int v=volume_get();if(v!=last_volume){last_volume=v;controls(true);}
    std::string state=restarting?"Seeking...":(!have_time||received_frames()==0?"Buffering...":(paused?"Paused":"Playing"));
    if(requested_options!=displayed_options)state="Next segment...";
    if(!stream_notice.empty())state="Switch failed";
    state+="  ·  Vol "+(v>=0?std::to_string(v)+"%":"--")+"  ·  S subtitles";
    lv_label_set_text(volume_label,state.c_str());lv_label_set_text(play_label,paused||pause_on_start?"Play":"Pause");
    lv_label_set_text(fit_label,(std::string(requested_options.width_fill?"Width":"Fit")+(requested_options.width_fill!=width_fill?" …":"")).c_str());
    if(subtitle_label)lv_label_set_text(subtitle_label,subtitle_track<0?"Sub: Off":!subtitle_error.empty()?"Sub: Error":subtitle_loading?"Sub: ...":"Sub: On");
}
static void close_subtitles(){
    if(subtitle_panel)lv_obj_delete(subtitle_panel);subtitle_panel=nullptr;subtitle_buttons.clear();
    if(video_root){screen::video_controls(controls_visible);lv_obj_invalidate(video_root);}
}
static void select_subtitle(int index){
    subtitle_track=index;subtitle_error.clear();subtitle_render_error.clear();subtitle_loading=index>=0;
    caption_window.reset();caption_cue=nullptr;caption_text.clear();screen::video_caption(nullptr,0,0);
    if(local_subtitles)local_subtitles->select(index);
    close_subtitles();controls(true);update_controls();
}
static void show_subtitles(){
    if(!video_root)return;if(subtitle_panel){close_subtitles();return;}
    subtitle_panel=lv_obj_create(video_root);lv_obj_set_pos(subtitle_panel,0,0);lv_obj_set_size(subtitle_panel,800,340);
    lv_obj_set_style_text_color(subtitle_panel,lv_color_hex(0xeaf1f8),0);lv_obj_set_style_bg_color(subtitle_panel,lv_color_hex(0x080e14),0);lv_obj_set_style_bg_opa(subtitle_panel,LV_OPA_COVER,0);lv_obj_set_style_pad_all(subtitle_panel,0,0);lv_obj_set_style_border_width(subtitle_panel,0,0);
    label(subtitle_panel,"Subtitles / 字幕",20,10,650);
    auto hint=label(subtitle_panel,subtitle_error.empty()?"Local subtitles · same size in Width / Fit":subtitle_error,20,44,570);
    if(small_font)lv_obj_set_style_text_font(hint,small_font,0);lv_obj_set_height(hint,32);
    auto size_change=[](lv_event_t*e){
        int delta=(int)(intptr_t)lv_event_get_user_data(e);subtitle_size=std::clamp(subtitle_size+delta,24,40);
        if(subtitle_font){lv_tiny_ttf_destroy(subtitle_font);subtitle_font=nullptr;}
        rendered_subtitle_size=0;close_subtitles();show_subtitles();
    };
    video_button(subtitle_panel,"A−",600,34,78,size_change,(void*)(intptr_t)-4);
    video_button(subtitle_panel,"A+",688,34,78,size_change,(void*)(intptr_t)4);
    auto list=lv_obj_create(subtitle_panel);lv_obj_set_pos(list,12,80);lv_obj_set_size(list,776,216);lv_obj_set_style_bg_opa(list,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(list,0,0);lv_obj_set_style_pad_all(list,0,0);
    subtitle_buttons.clear();subtitle_index=0;
    auto add=[&](const std::string&text,int index){
        auto l=video_button(list,text.c_str(),6,int(subtitle_buttons.size())*46,750,[](lv_event_t*e){select_subtitle((int)(intptr_t)lv_event_get_user_data(e)-1);},(void*)(intptr_t)(index+1));
        auto b=lv_obj_get_parent(l);
        lv_obj_set_width(l,718);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_center(l);
        if(index==subtitle_track){subtitle_index=subtitle_buttons.size();lv_obj_set_style_border_width(b,2,0);lv_obj_set_style_border_color(b,accent(),0);}
        subtitle_buttons.push_back(b);
    };
    add("Off / 关闭",-1);for(const auto&track:current.subtitles)
        add(std::to_string(track.index)+"  ·  "+track.title,track.index);
    label(subtitle_panel,current.subtitles.empty()?"No subtitle tracks in this source · Return: back":"J/K: select · Enter: apply · Return: back",20,309,760);
    controls(true);screen::video_controls(true,true);lv_obj_invalidate(video_root);
}
static void poll_subtitles(){
    if(!local_subtitles)return;
    local_subtitles->position(position);auto state=local_subtitles->state();subtitle_error=state.error.empty()?subtitle_render_error:state.error;subtitle_loading=state.loading;
    auto window=have_time&&!restarting?state.window:nullptr;
    auto cue=window?subtitles::bitmap_at(*window,position):nullptr;
    auto text=window?subtitles::text_at(*window,position):std::string();
    if(caption_window==window&&caption_cue==cue&&caption_text==text&&rendered_subtitle_size==subtitle_size)return;
    caption_window=window;caption_cue=cue;caption_text=text;rendered_subtitle_size=subtitle_size;subtitle_render_error.clear();
    subtitles::Image image;
    try{
        if(cue)image=subtitles::bitmap(*cue,subtitle_size);
        else if(!text.empty()){
            if(!subtitle_font){auto path="A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf";subtitle_font=lv_tiny_ttf_create_file(path.c_str(),subtitle_size);}
            if(!subtitle_font)throw std::runtime_error("Subtitle font unavailable");
            lv_point_t size;lv_text_get_size(&size,text.c_str(),subtitle_font,0,3,736,LV_TEXT_FLAG_NONE);
            image.width=752;image.height=std::clamp<int>(size.y+8,1,112);image.pixels.resize(size_t(image.width)*image.height,0);
            auto canvas=lv_canvas_create(video_root);lv_obj_add_flag(canvas,LV_OBJ_FLAG_HIDDEN);
            lv_canvas_set_buffer(canvas,image.pixels.data(),image.width,image.height,LV_COLOR_FORMAT_ARGB8888);
            lv_layer_t layer;lv_canvas_init_layer(canvas,&layer);
            lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.font=subtitle_font;d.text=text.c_str();d.text_local=1;d.align=LV_TEXT_ALIGN_CENTER;d.line_space=3;d.bidi_dir=LV_BASE_DIR_AUTO;
            // Black outline, then white glyphs on a transparent local layer.
            d.color=lv_color_hex(0x000000);
            for(int dy=-2;dy<=2;dy+=2)for(int dx=-2;dx<=2;dx+=2)if(dx||dy){lv_area_t a{8+dx,4+dy,743+dx,image.height-1};lv_draw_label(&layer,&d,&a);}
            d.color=lv_color_hex(0xffffff);lv_area_t a{8,4,743,image.height-1};lv_draw_label(&layer,&d,&a);
            lv_canvas_finish_layer(canvas,&layer);lv_obj_delete(canvas);
        }
        screen::video_caption(image.pixels.empty()?nullptr:image.pixels.data(),image.width,image.height);
    }catch(const std::exception&e){subtitle_render_error=subtitle_error=e.what();screen::video_caption(nullptr,0,0);}
}
static void toggle_pause(){
    if(restarting||!have_time){pause_on_start=!pause_on_start;paused=false;}
    else {command("pause\n");paused=!paused;if(!paused)frame_watchdog.resume(screen::tick());report_changed=true;}
    controls(true);update_controls();
}
static void seek_to(int64_t target){
    if(current.duration<=0||restarting)return;
    seek_target=std::clamp<int64_t>(target,0,std::max<int64_t>(0,current.duration-10000000));controls(true);update_controls();
}
static void seek_by(int seconds){seek_to((seek_target>=0?seek_target:position)+(int64_t)seconds*10000000);}
static void toggle_fit(){
    requested_options.width_fill=!requested_options.width_fill;
    if(segments)segments->request(requested_options);
    controls(true);update_controls();
}
static void halt_decoder(){
    if(segments){auto sessions=segments->stop();retired_sessions.insert(retired_sessions.end(),sessions.begin(),sessions.end());segments.reset();}
    if(!segment_fifo.empty()){unlink(segment_fifo.c_str());segment_fifo.clear();}
    if(player>0){command("quit\n");for(int i=0;i<20;i++){if(waitpid(player,nullptr,WNOHANG)==player){player=-1;break;}usleep(50000);}if(player>0){kill(player,SIGKILL);waitpid(player,nullptr,0);player=-1;}}
    if(player_group>0){kill(-player_group,SIGKILL);player_group=-1;}
    if(player_in>=0)close(player_in);if(player_out>=0)close(player_out);player_in=player_out=-1;
    if(video_pipe>=0)close(video_pipe);video_pipe=-1;
    if(!video_fifo.empty())unlink(video_fifo.c_str());video_fifo.clear();
}
static void cleanup_player(){
    local_subtitles.reset();subtitle_track=-1;caption_window.reset();caption_cue=nullptr;caption_text.clear();subtitle_error.clear();subtitle_render_error.clear();subtitle_loading=false;
    close_subtitles();halt_decoder();screen::playing=false;screen::tap=false;seek_target=-1;restarting=false;dragging=false;
    screen::video_end();if(video_root)lv_obj_delete(video_root);
    video_root=video_top=video_bar=progress=time_label=play_label=volume_label=fit_label=subtitle_label=nullptr;
    lv_obj_invalidate(lv_screen_active());unlink((c1::data()+"/streamplayer/playback.m3u").c_str());
}
static int supported_frame_output(){
    static int available=-1;if(available>=0)return available;
    int fd[2];if(pipe2(fd,O_CLOEXEC))return false;
    pid_t pid=fork();if(pid==0){dup2(fd[1],1);int n=open("/dev/null",O_RDWR);dup2(n,0);dup2(n,2);close(fd[0]);close(fd[1]);execl("/usr/bin/mplayer","mplayer","-noconfig","all","-vo","help",nullptr);_exit(127);}
    close(fd[1]);if(pid<0){close(fd[0]);return false;}
    fcntl(fd[0],F_SETFL,O_NONBLOCK);std::string output;uint32_t start=screen::tick();bool done=false;
    while(screen::tick()-start<2500){char buf[2048];ssize_t n;while((n=read(fd[0],buf,sizeof buf))>0)if(output.size()<32768)output.append(buf,n);if(waitpid(pid,nullptr,WNOHANG)==pid){done=true;break;}usleep(10000);}
    if(!done){kill(pid,SIGKILL);waitpid(pid,nullptr,0);}else{char buf[2048];ssize_t n;while((n=read(fd[0],buf,sizeof buf))>0)if(output.size()<32768)output.append(buf,n);}
    close(fd[0]);available=output.find("yuv4mpeg")!=std::string::npos?1:(output.find("\tnull\t")!=std::string::npos?2:0);return available;
}
static void start_player(const Playback&p){
    position=p.start;timeline.reset(p.start,p.duration);have_time=false;reported=false;segment_clock=false;segment_timeline=false;frame_width=frame_height=0;applied_change=-1;segment_state={};stream_notice.clear();
    requested_options=displayed_options=p.options;width_fill=p.options.width_fill;
    frame_error.clear();
    try {
    status("Starting video...");
    int output=supported_frame_output();
    if(!output){frame_error="MPlayer has no supported complete-frame output";cleanup_player();status(frame_error);ended=true;playback_failed=true;return;}
    decoder_pipe=output==2;
    video_reader=Y4mReader{};frame_error.clear();
    video_fifo=c1::data()+"/streamplayer/frames-"+std::to_string(getpid())+".y4m";
    if(mkfifo(video_fifo.c_str(),0600)!=0){cleanup_player();status("Cannot create video frame pipe");ended=true;playback_failed=true;return;}
    video_pipe=open(video_fifo.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);
    if(video_pipe<0){cleanup_player();status("Cannot open video frame pipe");ended=true;playback_failed=true;return;}
    // A complete encoded frame fits without a per-4KiB polling bottleneck.
    // Failure to enlarge is harmless; the normal pipe backpressure still applies.
    fcntl(video_pipe,F_SETPIPE_SZ,512*1024);
    std::string preload=c1::root()+"/streamplayer/c1max-yuv-pipe.so";
    if(decoder_pipe&&access(preload.c_str(),R_OK))throw std::runtime_error("YUV adapter unavailable");
    segment_fifo=c1::data()+"/streamplayer/segments-"+std::to_string(getpid())+".ts";
    if(mkfifo(segment_fifo.c_str(),0600))throw std::runtime_error("Cannot create segment pipe");
    // Decode at the negotiated low resolution. No decoder framebuffer access:
    // Both supported outputs send complete frames to the single compositor.
    const char *silent_test=std::getenv("C1_STREAMPLAYER_SILENT");
    const char *audio_output=silent_test&&std::string(silent_test)=="1"?"null":"media";
    std::vector<std::string> args={"mplayer","-noconfig","all","-slave","-quiet","-identify","-noconsolecontrols","-nolirc","-nojoystick","-nomouseinput","-nosub","-noautosub","-osdlevel","0","-demuxer","lavf","-lavfdopts","format=mpegts:probesize=65536:analyzeduration=1000000","-cache","64","-cache-min","10","-framedrop","-vo",decoder_pipe?"null":"yuv4mpeg:file="+video_fifo,"-ao",audio_output,"-vf","format=yv12",segment_fifo};
    std::vector<char*>av;for(auto&s:args)av.push_back(s.data());av.push_back(nullptr);
    int in[2],out[2];if(pipe2(in,O_CLOEXEC)){cleanup_player();status("Cannot open playback pipes");ended=true;playback_failed=true;return;}if(pipe2(out,O_CLOEXEC)){close(in[0]);close(in[1]);cleanup_player();status("Cannot open playback pipes");ended=true;playback_failed=true;return;}
    if(!screen::playing&&!screen::video_begin()){close(in[0]);close(in[1]);close(out[0]);close(out[1]);cleanup_player();status("Cannot prepare video framebuffer");ended=true;playback_failed=true;return;}
    screen::video_fit(width_fill);pid_t parent=getpid();player=fork();
    if(player==0){if(decoder_pipe){setenv("LD_PRELOAD",preload.c_str(),1);setenv("C1_YUV_FIFO",video_fifo.c_str(),1);}setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);dup2(in[0],0);dup2(out[1],1);dup2(out[1],2);close(in[0]);close(in[1]);close(out[0]);close(out[1]);execv("/usr/bin/mplayer",av.data());_exit(127);}
    close(in[0]);close(out[1]);if(player<0){close(in[1]);close(out[0]);cleanup_player();status("Cannot start player");ended=true;playback_failed=true;return;}
    setpgid(player,player);player_group=player;player_in=in[1];player_out=out[0];fcntl(player_out,F_SETFL,O_NONBLOCK);fcntl(player_in,F_SETFL,O_NONBLOCK);
    segments=std::make_unique<SegmentStream>(client,p,segment_fifo);
    if(!local_subtitles)local_subtitles=std::make_unique<SubtitleStream>(client,p);
    screen::playing=true;screen::tap=false;player_buffer.clear();position=p.start;timeline.reset(p.start,p.duration);have_time=false;paused=false;
    reported=false;ended=false;playback_failed=false;report_changed=false;restarting=false;
    player_start=last_query=last_report=last_frame_report=screen::tick();frame_watchdog.reset(player_start);create_controls();controls(true);update_controls();
    }catch(const std::exception&){
        cleanup_player();ended=true;playback_failed=true;frame_error="Cannot prepare playback files or decoder";status(frame_error);
    }
}
static void begin_seek(){
    auto target=seek_target;seek_target=-1;auto old=current;auto ticks=position;bool did=have_time;
    pause_on_start=paused||pause_on_start;restarting=true;screen::video_caption(nullptr,0,0);caption_window.reset();caption_cue=nullptr;caption_text.clear();halt_decoder();
    auto next=std::make_shared<Playback>();auto options=requested_options;auto retired=std::move(retired_sessions);retired_sessions.clear();
    work("Seeking...",[old,ticks,target,did,next,options,retired]{
        try{if(did)client.report(old,"Stopped",ticks);}catch(...){}
        for(auto&p:retired)try{client.stop_transcode(p);}catch(...){}
        client.stop_transcode(old);*next=client.playback(old.item,target,options);return Json::object();
    },[next](Json){current=*next;server_session_dirty=true;if(screen::tap){cleanup_player();ended=true;return;}start_player(current);});
    update_controls();
}
static void poll_player(){
    if(!screen::playing)return;
    auto now=screen::tick();
    if(segments){
        segment_state=segments->state();segments->position(position);
        if(segment_state.ready&&!segment_timeline){timeline.reset(segment_state.start,current.duration);position=segment_state.start;segment_timeline=true;}
        if(!segment_state.error.empty()){frame_error=segment_state.error;screen::tap=true;}
        stream_notice=segment_state.notice;
        if(!stream_notice.empty()){requested_options=displayed_options;}
    }
    if(video_pipe>=0){
        uint8_t data[32768];size_t budget=512*1024;ssize_t n;
        while(budget&&(n=read(video_pipe,data,std::min(sizeof data,budget)))>0){
            budget-=size_t(n);
            auto receive=[](const uint32_t*rgb,int w,int h,int an,int ad){
                if(segments&&segment_state.ready&&video_reader.pts()!=std::numeric_limits<int64_t>::min()){
                    const double pts=double(video_reader.pts())/90000.0;
                    if(!segment_clock){timeline.reset(segment_state.start,current.duration);timeline.set_origin(pts);segment_clock=true;}
                    timeline.update(pts);position=timeline.ticks();have_time=true;

                }
                bool geometry_changed=w!=frame_width||h!=frame_height;
                // Stock MPlayer discards AVPacket PTS before avcodec_decode_video2.
                // In that case use its slave clock, and gate layout changes on
                // the new decoded geometry so the old frame is never stretched.
                bool ultrawide=double(w)*an/ad/h>=800.0/340.0;
                for(const auto&change:segment_state.changes){
                    if(change.at<applied_change)continue;
                    bool shape=change.options.width_fill?(h>170||ultrawide):h<=170;
                    bool due=position>=change.at-500000||(geometry_changed&&change.at<=position+60000000);
                    if(shape&&due){displayed_options=change.options;applied_change=change.at;}
                }
                if(width_fill!=displayed_options.width_fill){width_fill=displayed_options.width_fill;screen::video_fit(width_fill);}
                if(geometry_changed)std::fprintf(stderr,"[video] frame geometry %dx%d mode=%s\n",w,h,width_fill?"Width":"Fit");
                frame_width=w;frame_height=h;
                screen::video_frame(rgb,w,h,an,ad);frame_watchdog.frame(screen::tick());
            };
            bool good=video_reader.feed(data,size_t(n),receive);
            if(!good){frame_error=video_reader.error();screen::tap=true;break;}
        }
    }
    if(player_out>=0){char b[1024];ssize_t n;while((n=read(player_out,b,sizeof b))>0){
        player_buffer.append(b,n);size_t e;while((e=player_buffer.find('\n'))!=std::string::npos){
            auto line=player_buffer.substr(0,e);player_buffer.erase(0,e+1);
            if(line.rfind("VIDEO:",0)==0||line.rfind("ID_VIDEO_",0)==0||line.rfind("AO:",0)==0||line.rfind("ID_AUDIO_",0)==0)
                std::fprintf(stderr,"[decoder] %.180s\n",line.c_str());
            if(line.rfind("C1_YUV_ERROR=",0)==0){
                auto reason=line.substr(13);if(!reason.empty()&&reason.back()=='\r')reason.pop_back();
                if(reason.size()<48&&reason.find_first_not_of("abcdefghijklmnopqrstuvwxyz_")==std::string::npos)
                    std::fprintf(stderr,"[video] adapter error: %s\n",reason.c_str());
                frame_error="Video decoder frame adapter failed";screen::tap=true;
            }
            if(!segment_clock&&line.rfind("ID_START_TIME=",0)==0){try{timeline.set_origin(std::stod(line.substr(14)));}catch(...){} }
            if(!segment_clock&&line.rfind("ANS_TIME_POSITION=",0)==0){try{
                if(timeline.update(std::stod(line.substr(18)))){
                    have_time=timeline.has_time();position=timeline.ticks();
                }
            }catch(...){} }
        }if(player_buffer.size()>8192)player_buffer.clear();
    }}
    // During a seek the old frame/time remain visible but there is no decoder.
    // Consume the pending pause only when the replacement decoder has a frame.
    if(player>0&&!restarting&&have_time&&received_frames()>0&&pause_on_start){command("pause\n");paused=true;pause_on_start=false;report_changed=true;}
    now=screen::tick(); // Frame callbacks may advance the watchdog clock.
    if(!segment_clock&&player>0&&now-last_query>(have_time?(subtitle_track>=0?100u:500u):100u)){command("pausing_keep_force get_time_pos\n");last_query=now;}
    poll_subtitles();
    if(now-last_controls>250){update_controls();last_controls=now;}
    if(controls_visible&&!subtitle_panel&&!paused&&!pause_on_start&&!restarting&&have_time&&!dragging&&screen::tick()-controls_touched>5000)controls(false);
    screen::video_refresh(paused&&have_time);
    int st=0;bool stopped=player>0&&waitpid(player,&st,WNOHANG)==player;if(stopped)player=-1;
    if(now-last_frame_report>=5000){
        std::fprintf(stderr,"[video] backend=%s frames=%llu elapsed_ms=%u position_ms=%lld paused=%d\n",decoder_pipe?"decode-pipe":"yuv4mpeg",(unsigned long long)received_frames(),now-player_start,(long long)(position/10000),int(paused));
        last_frame_report=now;
    }
    const uint32_t timeout_now=screen::tick();
    bool timeout=!restarting&&((!have_time&&timeout_now-player_start>60000)||(have_time&&frame_watchdog.stalled(timeout_now,paused)));
    if(timeout&&frame_error.empty())frame_error="Video frames stalled; playback stopped";
    if(stopped||screen::tap||timeout||interrupted){
        if(restarting&&busy){
            seek_cancelled=true;c1::cancel_requests();cleanup_player();ended=true;status("Stopping...");return;
        }
        bool failed=(stopped&&(!WIFEXITED(st)||WEXITSTATUS(st)!=0||!have_time))||timeout||!frame_error.empty();
        cleanup_player();ended=true;playback_failed=failed;status(!frame_error.empty()?frame_error:(failed?"Playback failed: decoder/output or server stream unavailable":"Playback stopped"));return;
    }
    if(restarting)return;
    if(seek_target>=0&&!busy){begin_seek();return;}
    if(have_time&&!busy&&((!reported&&now-last_report>1000)||report_changed||now-last_report>10000)){
        auto ticks=position;auto p=current;p.options=displayed_options;p.options.subtitle=subtitle_track;bool is_paused=paused;auto event=reported?"Progress":"";
        work("Playing...",[p,ticks,is_paused,event]{client.report(p,event,ticks,is_paused);return Json::object();},[](Json){reported=true;});last_report=now;report_changed=false;
    }
}
static void show_updates(){
    view=View::Updates;screen_base("C1Max Apps  /  GitHub updates");
    label(content,"Repository: zhuzhe1983/C1Max-Apps\nBranch: main  /  catalog.json\nChecks versions and source revisions. Same-version hash differences do not show which copy is newer. Installs remain manual.",8,8,730);
    button(content,"Check for updates",8,110,330,[](lv_event_t*){work("Checking GitHub...",[]{return c1::updates();},[](Json j){lv_obj_clean(content);physical_buttons.clear();physical_index=0;int y=4;int count=0,sources=0;for(auto&a:j){auto s=a.at("state").get<std::string>();if(s=="update"||s=="new")count++;else if(s=="source differs")sources++;label(content,a.at("id").get<std::string>()+"  "+a.at("version").get<std::string>()+"  ["+s+"]",8,y,730);y+=34;}std::string message;if(count)message=std::to_string(count)+" new app(s) or version(s); ";else message="No newer published versions. ";if(sources)message+=std::to_string(sources)+" same-version source hash(es) differ; direction unknown.";else message+="Installed apps are current.";status(message);});});
    status("Public update source; no GitHub token stored.");
}
static void show_roms(){
    view=View::Roms;screen_base("NES  /  Select ROM");std::vector<std::string> files;for(auto&e:fs::directory_iterator(c1::data()+"/nes/roms"))if(e.is_regular_file()&&e.path().extension()==".nes")files.push_back(e.path().string());std::sort(files.begin(),files.end());rows=files;
    int y=0;for(size_t i=0;i<files.size()&&i<128;i++){button(content,fs::path(files[i]).filename().string(),0,y,745,[](lv_event_t*e){auto path=rows.at((size_t)lv_event_get_user_data(e)).get<std::string>();auto bin=c1::root()+"/nes/c1max-nes";screen::close();execl(bin.c_str(),bin.c_str(),path.c_str(),nullptr);_exit(127);},(void*)i);y+=48;}
    status(files.empty()?"Copy .nes ROMs into /storage/apps/data/nes/roms":"Choose a ROM; tap Power for exit hint, hold 5s to exit");
}
static void physical_key(uint32_t key){
    if(key==screen::KEY_HOME){
        if(mode=="roms"){status("长按五秒电源键退出");return;}
        screen::quit=true;return;
    }
    if(key==screen::KEY_HOME_LONG){if(mode=="roms")screen::quit=true;return;}
    if(key==screen::KEY_MODE){if(active_field)status(screen::caps_lock()?"ABC | Double Shift: abc | Shift: symbols | Enter: next":"abc | Double Shift: ABC | Shift: symbols | Enter: next");return;}
    if(screen::playing){
        if(subtitle_panel){
            if(key==screen::KEY_EXIT||key=='s'){close_subtitles();return;}
            if(key=='j'||key=='d'||key==' ')subtitle_index=(subtitle_index+1)%subtitle_buttons.size();
            else if(key=='k'||key=='a')subtitle_index=(subtitle_index+subtitle_buttons.size()-1)%subtitle_buttons.size();
            else if(key==LV_KEY_ENTER){select_subtitle(subtitle_index?current.subtitles[subtitle_index-1].index:-1);return;}else return;
            for(size_t i=0;i<subtitle_buttons.size();++i){lv_obj_set_style_border_width(subtitle_buttons[i],i==subtitle_index?2:0,0);lv_obj_set_style_border_color(subtitle_buttons[i],accent(),0);}lv_obj_scroll_to_view(subtitle_buttons[subtitle_index],LV_ANIM_OFF);return;
        }
        if(key>='A'&&key<='Z')key+='a'-'A';
        if(key==screen::KEY_EXIT)screen::tap=true;
        else if(key==LV_KEY_ENTER||key==' ')toggle_pause();
        else if(key=='a')seek_by(-10);else if(key=='d')seek_by(10);
        else if(key=='q')seek_by(-60);else if(key=='e')seek_by(60);
        else if(key=='h')controls(!controls_visible);else if(key=='f')toggle_fit();else if(key=='v')toggle_mute();else if(key=='s')show_subtitles();
        return;
    }
    if(key==screen::KEY_EXIT){
        if(active_field){lv_obj_remove_state(active_field,LV_STATE_FOCUSED);active_field=nullptr;status("Input finished");return;}
        if(busy)return;
        if(view==View::Detail)render_items();else if(view==View::Items||view==View::Libraries)show_setup();
        return;
    }
    if(busy)return;
    if(view==View::Items){
        if(key=='a'&&page>0){--page;show_items();return;}
        if(key=='d'&&(page+1)*3<total_items){++page;show_items();return;}
        if((key=='w'||key=='s')&&!libraries.empty()){library_index=(library_index+libraries.size()+(key=='s'?1:-1))%libraries.size();parent_id=libraries[library_index].at("Id");page=0;show_items();return;}
    }
    if(active_field&&lv_obj_is_valid(active_field)){
        if(key==LV_KEY_ENTER){lv_obj_remove_state(active_field,LV_STATE_FOCUSED);if(active_field==server)active_field=username;else if(active_field==username)active_field=password;else active_field=nullptr;if(active_field)lv_obj_add_state(active_field,LV_STATE_FOCUSED);else status("Enter to connect; Space/J/K selects action");return;}
        if(key==LV_KEY_BACKSPACE){lv_textarea_delete_char(active_field);return;}
        if(key>=32&&key<127){char t[2]={(char)key,0};lv_textarea_add_text(active_field,t);}return;
    }
    if(key>='A'&&key<='Z')key+='a'-'A';
    if(key=='j'||key=='s'||key==' '||key==screen::KEY_SYMBOL)physical_index++;
    else if(key=='k'||key=='w')physical_index=physical_index?physical_index-1:physical_buttons.size()-1;
    else if(key==LV_KEY_ENTER){if(physical_index<physical_buttons.size()&&lv_obj_is_valid(physical_buttons[physical_index]))lv_obj_send_event(physical_buttons[physical_index],LV_EVENT_CLICKED,nullptr);return;}
    else return;
    if(physical_buttons.empty())return;physical_index%=physical_buttons.size();
    for(size_t i=0;i<physical_buttons.size();i++)if(lv_obj_is_valid(physical_buttons[i])){lv_obj_set_style_outline_width(physical_buttons[i],i==physical_index?2:0,0);lv_obj_set_style_outline_color(physical_buttons[i],lv_color_hex(0x79caff),0);}
    lv_obj_scroll_to_view(physical_buttons[physical_index],LV_ANIM_OFF);
}
int main(int argc,char**argv){
    signal(SIGINT,sig);signal(SIGTERM,sig);signal(SIGPIPE,SIG_IGN);
    if(argc>1&&std::string(argv[1])=="--check-updates"){try{std::cout<<c1::updates().dump(2)<<std::endl;return 0;}catch(std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}}
    if(argc>1&&std::string(argv[1])=="--updates")mode="updates";else if(argc>1&&std::string(argv[1])=="--roms")mode="roms";
    if(!screen::open())return 1;
    auto fontpath="A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf";font=lv_tiny_ttf_create_file(fontpath.c_str(),20);if(font)font->fallback=&lv_font_montserrat_18;
    small_font=lv_tiny_ttf_create_file(fontpath.c_str(),16);large_font=lv_tiny_ttf_create_file(fontpath.c_str(),24);
    client.load();if(mode=="updates")show_updates();else if(mode=="roms")show_roms();else show_setup();
    while(!screen::quit&&!interrupted){
        lv_timer_handler();
        for(uint32_t key;!screen::quit&&!interrupted&&(key=screen::take_key())!=0;)physical_key(key);
        if(screen::quit||interrupted)break;
        if(busy&&job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            auto r=job.get();busy=false;if(content)lv_obj_remove_state(content,LV_STATE_DISABLED);
            if(seek_cancelled){seek_cancelled=false;c1::reset_requests();completed={};}
            else if(r["ok"].get<bool>()){
                auto done=std::move(completed);
                try{done(r["result"]);}catch(std::exception&){status("Unexpected server response; please retry");if(screen::playing){cleanup_player();ended=true;playback_failed=true;}}
            }else{status(r["error"].get<std::string>());if(restarting){cleanup_player();ended=true;playback_failed=true;}}
        }
        poll_player();
        if(ended&&!busy){
            ended=false;auto p=current;auto ticks=position;bool did=reported||have_time;auto retired=std::move(retired_sessions);retired_sessions.clear();
            work("Stopping server session...",[p,ticks,did,retired]{for(auto&r:retired)try{client.stop_transcode(r);}catch(...){}try{if(did)client.report(p,"Stopped",ticks);}catch(...){}client.stop_transcode(p);return Json::object();},[](Json){server_session_dirty=false;status(playback_failed?(!frame_error.empty()?frame_error:"Playback failed: stream or decoder unavailable"):"Stopped. Choose another video.");});
        }
        usleep(10000);
    }
    bool was_playing=screen::playing;cleanup_player();c1::cancel_requests();if(job.valid())job.wait();c1::reset_requests(900);if(was_playing||ended||server_session_dirty){try{if(reported||have_time)client.report(current,"Stopped",position);}catch(...){}try{client.stop_transcode(current);}catch(...){}}
    for(auto&p:retired_sessions)try{client.stop_transcode(p);}catch(...){}
    unlink((c1::data()+"/streamplayer/playback.m3u").c_str());if(audio_mixer)mixer_close(audio_mixer);screen::close();return 0;
}
