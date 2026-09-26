#include "api.hpp"
#include "player.hpp"
#include "display.hpp"
#include "lv_tiny_ttf.h"
#include <algorithm>
#include <atomic>
#include <csignal>
#include <deque>
#include <filesystem>
#include <future>
#include <iostream>
#include <tinyalsa/mixer.h>
#include <unistd.h>
namespace {
constexpr uint32_t bg=0x0e1018,surface=0x181b28,line=0x2b3043,ink=0xf5f5fa,muted=0xaeb5c9,pink=0xfb7299;
enum class Page { Home, Portrait, Search, Saved, History, Account, Detail };
Page page=Page::Home,previous=Page::Home;
bili::Api api;bili::Player player;
Json items=Json::array(),detail,store={{"saved",Json::array()},{"history",Json::array()}};
std::string query,status="连接 B 站…",account_name="游客",qr_key,qr_url,video_notice;
int api_page=1,offset=0,selected=0,part=0,current_index=0;bool portrait_mode=false,changing_video=false,change_was_paused=false;int overlay_mode=-1;bool more=false,busy=false,cancelled=false,playing=false,controls=true,fill=true,dragging=false;
uint32_t qr_next=0,qr_expires=0,controls_at=0,last_overlay=0,notice_at=0;int remembered_volume=50;
std::future<Json> job;std::function<void(Json)> complete;
std::deque<std::function<void()>> actions;
lv_font_t *font=nullptr,*small=nullptr,*large=nullptr;
lv_obj_t *footer=nullptr,*field=nullptr,*video_top=nullptr,*video_bar=nullptr,*video_progress=nullptr,*video_time=nullptr,*video_state=nullptr,*video_pause=nullptr,*video_fit=nullptr,*video_loading=nullptr;
struct mixer*audio=nullptr;volatile sig_atomic_t interrupted=0;
void signal_stop(int){interrupted=1;}
void paint();void load_list();void start_video();void open_detail(int index,bool autoplay=false);void adjacent_video(int delta);void rotate_video();void video_ui(bool reveal=true);void show_controls(bool show);void navigate(Page p);void covers();void make_qr();void stop_video();
std::string path(const char*name){return c1::data()+"/bilibili/"+name;}
std::string clock_text(int seconds){char b[32];std::snprintf(b,sizeof b,"%d:%02d",std::max(0,seconds)/60,std::max(0,seconds)%60);return b;}
void message(const std::string&s){status=s;if(footer)lv_label_set_text(footer,s.c_str());}
lv_obj_t*box(lv_obj_t*p,int x,int y,int w,int h,uint32_t color=surface,int radius=10){auto*o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,radius,0);return o;}
lv_obj_t*text(lv_obj_t*p,const std::string&s,int x,int y,int w,int h=26,uint32_t color=ink,lv_font_t*f=nullptr){auto*o=lv_label_create(p);lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(o,lv_color_hex(color),0);if(f)lv_obj_set_style_text_font(o,f,0);return o;}
lv_obj_t*button(lv_obj_t*p,const std::string&s,int x,int y,int w,int h,std::function<void()>fn,bool primary=false){auto*o=box(p,x,y,w,h,primary?pink:surface,8);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_bg_color(o,lv_color_hex(0xa34666),LV_STATE_PRESSED);actions.push_back(std::move(fn));lv_obj_add_event_cb(o,[](lv_event_t*e){auto fn=*static_cast<std::function<void()>*>(lv_event_get_user_data(e));fn();},LV_EVENT_CLICKED,&actions.back());auto*l=text(o,s,8,(h-24)/2,w-16,24,primary?0x14121b:ink);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);return o;}
void work(const std::string&msg,std::function<Json()>run,std::function<void(Json)>done){if(busy)return;c1::reset_requests();busy=true;cancelled=false;message(msg);complete=std::move(done);job=std::async(std::launch::async,[run]{try{return Json{{"ok",true},{"data",run()}};}catch(const std::exception&e){return Json{{"ok",false},{"error",e.what()}};}});}
void save_store(){try{c1::save_private(path("library.json"),store.dump(2));}catch(const std::exception&e){message(e.what());}}
void remember(const char*kind,const Json&v){auto row=bili::summary(v);auto&list=store[kind];for(auto it=list.begin();it!=list.end();)if(it->value("bvid","")==row["bvid"])it=list.erase(it);else ++it;list.insert(list.begin(),row);while(list.size()>100)list.erase(list.end()-1);save_store();}
bool saved(){if(!detail.is_object())return false;for(auto&v:store["saved"])if(v.value("bvid","")==detail.value("bvid",""))return true;return false;}
void toggle_saved(){if(busy)return;if(saved()){auto&list=store["saved"];for(auto it=list.begin();it!=list.end();)if(it->value("bvid","")==detail.value("bvid",""))it=list.erase(it);else ++it;save_store();message("已从本机收藏删除");}else{remember("saved",detail);message("已加入本机收藏");}paint();}
void photo(lv_obj_t*p,const Json&v,int x,int y){box(p,x,y,192,108,0x272238,7);text(p,LV_SYMBOL_PLAY,x+72,y+34,48,35,pink,large);auto file=v.value("poster",std::string());if(!file.empty()){auto*im=lv_image_create(p);lv_image_set_src(im,("A:"+file).c_str());lv_obj_set_pos(im,x,y);lv_obj_set_size(im,192,108);lv_image_set_inner_align(im,LV_IMAGE_ALIGN_CONTAIN);}}
void base(){auto*r=lv_screen_active();lv_obj_clean(r);actions.clear();field=nullptr;footer=nullptr;lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(r,lv_color_hex(bg),0);lv_obj_set_style_bg_opa(r,LV_OPA_COVER,0);lv_obj_set_style_text_color(r,lv_color_hex(ink),0);if(font)lv_obj_set_style_text_font(r,font,0);
    text(r,"bilibili",18,10,128,34,pink,large);text(r,"C1 / VIDEO",150,17,205,24,muted,small);button(r,account_name+" · 账号",607,4,176,44,[]{if(!busy)navigate(Page::Account);},page==Page::Account);
    static const char*names[]={"热门","竖屏","搜索","本机收藏","观看记录"};for(int i=0;i<5;i++){auto*b=button(r,names[i],12,58+i*49,122,44,[i]{if(!busy)navigate(Page(i));},page==Page(i));(void)b;}
    footer=text(r,status,151,314,630,22,muted,small);
}
void paint(){if(playing)return;base();auto*r=lv_screen_active();
    if(page==Page::Account){
        text(r,"扫码登录",164,59,315,34,ink,large);
        if(!qr_url.empty()){
            auto*q=lv_qrcode_create(r);lv_qrcode_set_size(q,208);lv_qrcode_set_dark_color(q,lv_color_hex(0x111111));lv_qrcode_set_light_color(q,lv_color_hex(0xffffff));lv_obj_set_pos(q,169,96);lv_obj_set_style_border_width(q,10,0);lv_obj_set_style_border_color(q,lv_color_hex(0xffffff),0);lv_qrcode_update(q,qr_url.data(),qr_url.size());
            text(r,"打开手机哔哩哔哩\n扫描左侧二维码\n并在手机上确认登录",418,105,344,100,ink);button(r,"刷新二维码",421,244,156,44,[]{if(!busy)make_qr();});
        }else{
            text(r,account_name=="游客"?"游客可以浏览和播放公开视频。\n登录信息仅保存在这台设备上。":"当前账号："+account_name+"\n收藏和观看记录仍保存在本机。",166,112,590,92,muted);
            button(r,"生成登录二维码",165,237,225,48,[]{if(!busy)make_qr();},true);
        }
        if(account_name!="游客")button(r,"退出账号",606,244,155,44,[]{if(busy)return;api.cookies=Json::object();c1::save_private(path("session.json"),"{}");account_name="游客";qr_url.clear();qr_key.clear();message("已删除本机登录凭据");paint();});return;
    }
    if(page==Page::Detail){
        photo(r,detail,153,59);text(r,detail.value("title",""),360,56,421,59,ink,font);text(r,detail.value("author","")+"  ·  "+detail.value("bvid",""),360,116,421,25,muted,small);
        text(r,detail.value("desc",""),153,178,348,116,muted,small);
        auto&parts=detail["pages"];int first=(part/2)*2;for(int i=first;i<std::min<int>(first+2,parts.size());i++){auto*b=button(r,"P"+std::to_string(i+1)+"  "+parts[i].value("part",""),514,155+(i-first)*49,269,43,[i]{if(!busy){part=i;paint();}},i==part);(void)b;}
        button(r,"播放 P"+std::to_string(part+1),514,262,133,44,[]{if(!busy)start_video();},true);button(r,saved()?"取消收藏":"收藏",658,262,125,44,[]{toggle_saved();});return;
    }
    int top=61;
    if(page==Page::Search){
        field=lv_textarea_create(r);lv_obj_set_pos(field,152,54);lv_obj_set_size(field,479,43);lv_textarea_set_one_line(field,true);lv_textarea_set_max_length(field,160);lv_textarea_set_placeholder_text(field,"关键词 / BV 号 / 视频链接");lv_textarea_set_text(field,query.c_str());lv_obj_set_style_bg_color(field,lv_color_hex(surface),0);lv_obj_set_style_bg_opa(field,LV_OPA_COVER,0);lv_obj_set_style_text_color(field,lv_color_hex(ink),0);lv_obj_set_style_border_color(field,lv_color_hex(line),0);lv_obj_set_style_border_color(field,lv_color_hex(pink),LV_STATE_FOCUSED);lv_obj_set_style_border_width(field,1,0);lv_obj_set_style_pad_all(field,8,0);
        lv_obj_add_event_cb(field,[](lv_event_t*){if(!busy)lv_obj_add_state(field,LV_STATE_FOCUSED);},LV_EVENT_CLICKED,nullptr);
        button(r,"搜索",642,54,141,43,[]{if(busy)return;query=lv_textarea_get_text(field);api_page=1;offset=selected=0;load_list();},true);top=104;
    }else text(r,page==Page::Home?"此刻热门":page==Page::Portrait?"竖屏 · 热门精选":page==Page::Saved?"本机收藏":"最近观看",153,56,376,30,ink,large);
    int y=page==Page::Search?104:98,h=page==Page::Search?176:198;
    for(int i=0;i<3&&offset+i<int(items.size());i++){
        int index=offset+i;auto&v=items[index];auto*b=button(r,"",151+i*214,y,204,h,[index]{if(!busy)open_detail(index);});lv_obj_set_style_border_width(b,selected==i?2:1,0);lv_obj_set_style_border_color(b,lv_color_hex(selected==i?pink:line),0);photo(b,v,6,6);
        text(b,v.value("title",""),9,117,186,54,ink,font);if(page!=Page::Search)text(b,v.value("author",""),9,173,186,22,muted,small);
    }
    if(items.empty())text(r,busy?"正在加载视频…":page==Page::Search?"输入关键词或 BV 号开始搜索":page==Page::Saved?"收藏的视频会保存在这里。":page==Page::History?"播放过的视频会出现在这里。":page==Page::Portrait?"本页暂无竖屏视频 · D 下一页 / R 刷新":"暂时没有视频，按 R 重试",165,150,605,85,muted);
    if(page!=Page::Search){button(r,"‹",636,50,62,42,[]{if(busy)return;if(offset>=3){offset-=3;selected=0;paint();covers();}else if(api_page>1&&(page==Page::Home||page==Page::Portrait||page==Page::Search)){--api_page;offset=selected=0;load_list();}});button(r,"›",713,50,70,42,[]{if(busy)return;if(offset+3<int(items.size())){offset+=3;selected=0;paint();covers();}else if(more){++api_page;offset=selected=0;load_list();}});}
    if(page==Page::Search){text(r,"A/D 翻页 · J/K 选择 · Enter 打开",155,282,489,22,muted,small);text(r,std::to_string(api_page)+" / "+std::to_string(offset/3+1),678,282,100,22,muted,small);}
    (void)top;
}
void covers(){if(busy||items.empty())return;Json visible=Json::array();for(int i=offset;i<std::min<int>(offset+3,items.size());i++)visible.push_back(items[i]);int start=offset;
    work("加载封面…",[visible]{Json out=Json::array();for(auto v:visible){try{v["poster"]=api.poster(v);}catch(...){}out.push_back(v);}return out;},[start](Json out){for(size_t i=0;i<out.size()&&start+int(i)<int(items.size());i++)items[start+i]=out[i];message("W/S 分类 · A/D 翻页 · J/K 选择 · Enter 打开");paint();});
}
bool remote_list(Page p){return p==Page::Home||p==Page::Portrait||p==Page::Search;}
Json fetch_list(Page p,const std::string&q,int n){return p==Page::Search?api.search(q,n):p==Page::Portrait?api.portrait(n):api.popular(n);}
void load_list(){
    if(page==Page::Saved||page==Page::History){items=store[page==Page::Saved?"saved":"history"];more=false;paint();covers();return;}
    bool cached_page=page==Page::Home||page==Page::Portrait;const char*cache=page==Page::Portrait?"portrait.json":"home.json";
    if(cached_page&&api_page==1&&items.empty())try{auto cached=Json::parse(c1::read_file(path(cache),98304));for(auto&v:cached){auto row=bili::summary(v);if(page==Page::Portrait&&!(row.value("width",0)>0&&row.value("height",0)>row.value("width",0)))continue;auto poster=path("posters")+"/"+row.at("bvid").get<std::string>()+".png";if(std::filesystem::is_regular_file(poster))row["poster"]=poster;items.push_back(row);if(items.size()==(page==Page::Portrait?60u:20u))break;}message("显示上次缓存，正在刷新…");paint();}catch(...){}
    auto source=page;auto q=query;auto n=api_page;work(cached_page&&api_page==1&&!items.empty()?"显示缓存，正在刷新…":"正在加载视频…",[source,q,n]{return fetch_list(source,q,n);},[](Json j){items=j.at("items");more=j.value("more",false);offset=selected=0;if((page==Page::Home||page==Page::Portrait)&&api_page==1)try{c1::save_private(path(page==Page::Portrait?"portrait.json":"home.json"),items.dump());}catch(...){}message(items.empty()?(page==Page::Portrait?"本页暂无竖屏视频 · D 下一页 / R 刷新":"没有找到视频"):"加载封面…");paint();covers();});
}
void navigate(Page p){if(busy)return;qr_url.clear();qr_key.clear();page=p;items=Json::array();offset=selected=0;api_page=1;message("W/S 分类 · Enter 打开");paint();if(p==Page::Account){work("检查登录状态…",[]{return api.account();},[](Json j){account_name=j.value("logged_in",false)?j.value("name","已登录"):"游客";message("使用手机哔哩哔哩扫码登录");paint();});}
    else if(p==Page::Search){lv_obj_add_state(field,LV_STATE_FOCUSED);message("实体键输入 · 双击 Shift 大写 · Enter 搜索");}else load_list();
}
void present_detail(Json j,Page source,int index,bool autoplay){
    detail=std::move(j);part=0;current_index=index;offset=index/3*3;selected=index%3;previous=source;page=Page::Detail;
    if(source==Page::Portrait&&!autoplay)portrait_mode=true;
    message("Enter 播放 · P 上个 / O 下个 · W/S 分 P");paint();if(autoplay)start_video();
}
Json fetch_detail(const std::string&id){auto d=api.detail(id);try{d["poster"]=api.poster(d);}catch(...){}return d;}
void open_detail(int index,bool autoplay){
    if(busy||index<0||index>=int(items.size()))return;
    auto id=items[index].at("bvid").get<std::string>();auto source=page==Page::Detail?previous:page;
    if(playing)stop_video();
    work(autoplay?"正在切换视频…":"加载视频详情…",[id]{return fetch_detail(id);},[source,index,autoplay](Json j){present_detail(std::move(j),source,index,autoplay);});
}
void adjacent_video(int delta){
    if(busy||page!=Page::Detail)return;int index=current_index+delta;bool autoplay=playing;
    bool within=index>=0&&index<int(items.size());
    bool can_page=remote_list(previous)&&(delta>0?more:api_page>1);
    if(autoplay&&(within||can_page)){
        // Keep the current decoder/frame and orientation while resolving the
        // next clip. Only replace them after detail and playurl both succeed.
        change_was_paused=player.paused;if(!player.paused)player.pause();changing_video=true;show_controls(false);
        auto source=previous;auto q=query;int next_page=api_page+(within?0:delta);auto id=within?items[index].at("bvid").get<std::string>():std::string();
        work("正在切换视频…",[source,q,next_page,id,index,delta]{
            Json out=Json::object();int target=index;std::string next_id=id;
            if(next_id.empty()){auto list=fetch_list(source,q,next_page);if(list["items"].empty())throw std::runtime_error("相邻页没有匹配视频，请返回列表翻页");target=delta>0?0:int(list["items"].size())-1;next_id=list["items"][target].at("bvid");out["listing"]=std::move(list);}
            auto d=fetch_detail(next_id);auto media=api.stream(next_id,d["pages"][0].at("cid").get<int64_t>());
            out["detail"]=std::move(d);out["index"]=target;out["stream"]={{"url",media.url},{"duration",media.duration},{"quality",media.quality}};return out;
        },[source,next_page](Json out){
            auto media=out.at("stream");try{player.start({media.at("url"),media.at("duration"),media.at("quality")});}catch(const std::exception&e){changing_video=false;stop_video();message(e.what());return;}
            if(out.contains("listing")){items=out["listing"].at("items");more=out["listing"].value("more",false);api_page=next_page;}
            detail=out.at("detail");current_index=out.at("index");offset=current_index/3*3;selected=current_index%3;part=0;previous=source;page=Page::Detail;
            changing_video=false;remember("history",detail);screen::video_fit(fill);video_ui(false);
        });return;
    }
    if(within){open_detail(index,autoplay);return;}
    bool across=remote_list(previous)&&(delta>0?more:api_page>1);
    if(!across){status=delta>0?"已是列表最后一个视频":"已是列表第一个视频";if(playing){show_controls(true);video_notice=status;notice_at=screen::tick();}else message(status);return;}
    if(playing)stop_video();auto source=previous;auto q=query;int next_page=api_page+delta;
    work("正在加载相邻列表…",[source,q,next_page,delta]{auto j=fetch_list(source,q,next_page);auto&list=j.at("items");if(!list.empty()){int index=delta>0?0:int(list.size())-1;j["detail"]=fetch_detail(list[index].at("bvid").get<std::string>());j["index"]=index;}return j;},[source,next_page,autoplay](Json j){
        auto next=j.at("items");if(next.empty()){message("相邻页没有匹配视频 · 返回列表后继续翻页");return;}
        // Commit the new page only after its selected detail is available.
        // Cancellation/network failures leave the old queue and detail aligned.
        items=std::move(next);more=j.value("more",false);api_page=next_page;
        present_detail(j.at("detail"),source,j.at("index"),autoplay);
    });
}
void make_qr(){work("正在生成登录二维码…",[]{return api.qr();},[](Json j){qr_key=j.at("key");qr_url=j.at("url");qr_next=screen::tick()+3000;qr_expires=screen::tick()+170000;message("请用手机哔哩哔哩扫码");paint();});}
int volume(){if(!audio)audio=mixer_open(0);auto*c=audio?mixer_get_ctl_by_name(audio,"softvolume"):nullptr;long values[2];if(!c||mixer_ctl_get_array(c,values,2))return -1;int lo=mixer_ctl_get_range_min(c),hi=mixer_ctl_get_range_max(c);return hi>lo?std::clamp<int>((values[0]-lo)*100/(hi-lo),0,100):-1;}
void set_volume(int value){if(volume()<0)return;auto*c=mixer_get_ctl_by_name(audio,"softvolume");int lo=mixer_ctl_get_range_min(c),hi=mixer_ctl_get_range_max(c);value=std::clamp(value,0,100);long v[2]={lo+(hi-lo)*value/100,lo+(hi-lo)*value/100};mixer_ctl_set_array(c,v,2);}
void sync_video_overlay(){
    if(!playing)return;int mode=controls?1:(changing_video||!player.loaded)?2:0;if(mode==overlay_mode)return;overlay_mode=mode;
    for(auto*o:{video_top,video_bar})if(o){if(mode==1)lv_obj_remove_flag(o,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(o,LV_OBJ_FLAG_HIDDEN);}
    if(video_loading){if(mode==2)lv_obj_remove_flag(video_loading,LV_OBJ_FLAG_HIDDEN);else lv_obj_add_flag(video_loading,LV_OBJ_FLAG_HIDDEN);}
    screen::video_controls_area(portrait_mode?60:56,mode==2?(portrait_mode?800:340):(portrait_mode?526:218));
    screen::video_controls(mode!=0);
}
void show_controls(bool show){if(changing_video&&show)return;controls=show;controls_at=screen::tick();sync_video_overlay();}
void abort_change(const std::string&reason){
    changing_video=false;if(!change_was_paused&&player.paused)player.pause();
    video_notice=reason;notice_at=screen::tick();show_controls(true);message(reason);
}
void overlay(){if(!playing)return;auto now=screen::tick();if(now-last_overlay<250)return;last_overlay=now;sync_video_overlay();if(!dragging&&video_progress&&player.duration>0)lv_slider_set_value(video_progress,std::clamp(int(player.position*1000/player.duration),0,1000),LV_ANIM_OFF);
    if(video_time)lv_label_set_text(video_time,(clock_text(player.position)+" / "+clock_text(player.duration)).c_str());
    if(video_state)lv_label_set_text(video_state,(!video_notice.empty()&&now-notice_at<2500?video_notice:((player.loaded?"360p 直连":"正在连接 / 缓冲…")+std::string("   ·   音量 ")+std::to_string(volume()))).c_str());
    if(video_pause)lv_label_set_text(video_pause,player.paused?"继续":"暂停");if(video_fit)lv_label_set_text(video_fit,fill?"填满宽度":"完整画面");
    if(controls&&player.loaded&&!player.paused&&!dragging&&now-controls_at>4500)show_controls(false);
}
void video_ui(bool reveal){
    screen::portrait(portrait_mode);int w=portrait_mode?340:800,h=portrait_mode?800:340,top=portrait_mode?60:56,bar_y=portrait_mode?526:218;
    screen::video_controls_area(top,bar_y);
    auto*r=lv_screen_active();lv_obj_clean(r);actions.clear();footer=field=nullptr;dragging=false;
    video_top=video_bar=video_progress=video_time=video_state=video_pause=video_fit=video_loading=nullptr;overlay_mode=-1;
    auto*hit=lv_obj_create(r);lv_obj_remove_style_all(hit);lv_obj_set_size(hit,w,h);lv_obj_add_flag(hit,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(hit,[](lv_event_t*){show_controls(!controls);},LV_EVENT_CLICKED,nullptr);
    video_top=box(r,0,0,w,top,bg,0);video_bar=box(r,0,bar_y,w,h-bar_y,bg,0);
    text(video_top,detail.value("title","视频"),12,portrait_mode?8:14,portrait_mode?316:470,portrait_mode?44:26,ink,small);
    auto action=[&](lv_obj_t*parent,const char*label,int x,int y,int width,std::function<void()>fn){return button(parent,label,x,y,width,44,[fn]{controls_at=screen::tick();fn();});};
    auto mute=[]{int v=volume();if(v>0){remembered_volume=v;set_volume(0);}else set_volume(remembered_volume);};
    auto fit=[]{fill=!fill;screen::video_fit(fill);};
    if(!portrait_mode){action(video_top,"P 上个",494,6,90,[]{adjacent_video(-1);});action(video_top,"O 下个",594,6,90,[]{adjacent_video(1);});action(video_top,"L 竖屏",694,6,94,[]{rotate_video();});}
    video_progress=lv_slider_create(video_bar);lv_obj_set_pos(video_progress,22,14);lv_obj_set_size(video_progress,w-44,12);lv_slider_set_range(video_progress,0,1000);lv_obj_set_ext_click_area(video_progress,12);lv_obj_set_style_bg_color(video_progress,lv_color_hex(pink),LV_PART_INDICATOR);lv_obj_add_event_cb(video_progress,[](lv_event_t*e){auto c=lv_event_get_code(e);if(c==LV_EVENT_PRESSED){dragging=true;controls_at=screen::tick();}if(c==LV_EVENT_RELEASED){dragging=false;controls_at=screen::tick();player.seek(player.duration*lv_slider_get_value(video_progress)/1000.0);}if(c==LV_EVENT_PRESS_LOST)dragging=false;},LV_EVENT_ALL,nullptr);
    video_time=text(video_bar,"",18,34,portrait_mode?304:220,25,ink,small);video_state=text(video_bar,"正在连接 / 缓冲…",portrait_mode?18:265,portrait_mode?62:34,portrait_mode?304:515,25,muted,small);
    if(portrait_mode){
        action(video_bar,"P 上个",12,96,98,[]{adjacent_video(-1);});auto*b=action(video_bar,"暂停",121,96,98,[]{player.pause();});video_pause=lv_obj_get_child(b,0);action(video_bar,"O 下个",230,96,98,[]{adjacent_video(1);});
        action(video_bar,"−10s",12,148,98,[]{player.seek(player.position-10);});b=action(video_bar,"完整画面",121,148,98,fit);video_fit=lv_obj_get_child(b,0);action(video_bar,"+10s",230,148,98,[]{player.seek(player.position+10);});
        action(video_bar,"静音",12,200,98,mute);action(video_bar,"L 横屏",121,200,98,[]{rotate_video();});action(video_bar,"停止",230,200,98,[]{stop_video();});
        text(video_bar,"音量 − / ＋ 使用实体按键",18,248,304,22,muted,small);
    }else{
        action(video_bar,"−10s",12,69,82,[]{player.seek(player.position-10);});auto*b=action(video_bar,"暂停",104,69,94,[]{player.pause();});video_pause=lv_obj_get_child(b,0);
        action(video_bar,"+10s",208,69,82,[]{player.seek(player.position+10);});action(video_bar,"静音",300,69,84,mute);
        action(video_bar,"−",394,69,47,[]{set_volume(volume()-5);});action(video_bar,"+",451,69,47,[]{set_volume(volume()+5);});b=action(video_bar,"填满宽度",508,69,155,fit);video_fit=lv_obj_get_child(b,0);
        action(video_bar,"停止",674,69,113,[]{stop_video();});
    }
    video_loading=box(r,0,0,w,top,bg,0);text(video_loading,"正在切换 / 加载视频…",12,portrait_mode?18:14,w-24,26,ink,small);
    video_notice.clear();show_controls(reveal);last_overlay=0;overlay();lv_refr_now(lv_display_get_default());
}
void rotate_video(){if(!playing)return;portrait_mode=!portrait_mode;video_ui();}

void start_video(){auto id=detail.at("bvid").get<std::string>();auto cid=detail["pages"][part].at("cid").get<int64_t>();
    work("正在解析 360p 播放源…",[id,cid]{auto s=api.stream(id,cid);return Json{{"url",s.url},{"duration",s.duration},{"quality",s.quality}};},[](Json j){remember("history",detail);try{player.start({j.at("url"),j.value("duration",0),j.value("quality",16)});playing=true;fill=true;screen::video_fit(fill);video_ui();}catch(const std::exception&e){message(e.what());}});
}
void stop_video(){if(!playing)return;if(changing_video){cancelled=true;c1::cancel_requests();changing_video=false;}player.stop();playing=false;screen::portrait(false);video_top=video_bar=video_progress=video_time=video_state=video_pause=video_fit=video_loading=nullptr;overlay_mode=-1;message(player.error.empty()?"已停止 · Enter 重新播放":player.error);paint();}
void key(uint32_t k){
    if(k==screen::KEY_HOME){screen::quit=true;return;}
    if(playing){if(k>='A'&&k<='Z')k+='a'-'A';controls_at=screen::tick();if(changing_video){if(k==screen::KEY_EXIT){cancelled=true;c1::cancel_requests();}return;}if(k=='l'){rotate_video();return;}if(k=='p'||k=='o'){adjacent_video(k=='p'?-1:1);return;}if(k==screen::KEY_EXIT){stop_video();return;}if(k==LV_KEY_ENTER||k==' ')player.pause();else if(k=='a'||k==LV_KEY_LEFT)player.seek(player.position-10);else if(k=='d'||k==LV_KEY_RIGHT)player.seek(player.position+10);else if(k=='q')player.seek(player.position-60);else if(k=='e')player.seek(player.position+60);else if(k=='f'){fill=!fill;screen::video_fit(fill);}else if(k=='h')show_controls(!controls);else if(k=='v'){int v=volume();if(v>0){remembered_volume=v;set_volume(0);}else set_volume(remembered_volume);}return;}
    if(busy){if(k==screen::KEY_EXIT){cancelled=true;c1::cancel_requests();message("正在取消…");}return;}
    if(k==screen::KEY_MODE){message(screen::caps_lock()?"ABC · 双击 Shift 切回小写":"abc · Shift 符号 · 双击 Shift 大写");return;}
    if(field&&lv_obj_has_state(field,LV_STATE_FOCUSED)){
        if(k==screen::KEY_EXIT){query=lv_textarea_get_text(field);lv_obj_remove_state(field,LV_STATE_FOCUSED);message("Enter 打开视频 · 点击输入框继续编辑");}
        else if(k==LV_KEY_ENTER){query=lv_textarea_get_text(field);lv_obj_remove_state(field,LV_STATE_FOCUSED);api_page=1;offset=selected=0;load_list();}
        else if(k==LV_KEY_BACKSPACE)lv_textarea_delete_char(field);else if(k>=32&&k<127){char s[]={char(k),0};lv_textarea_add_text(field,s);}return;
    }
    if(k==screen::KEY_EXIT){if(page==Page::Detail){page=previous;message("W/S 分类 · J/K 选择 · Enter 打开");paint();}else if(page!=Page::Home)navigate(Page::Home);return;}
    if(page==Page::Detail){if(k=='p'||k=='P'||k=='o'||k=='O'){adjacent_video(k=='p'||k=='P'?-1:1);return;}if(k=='w'||k==LV_KEY_UP)part=std::max(0,part-1);else if(k=='s'||k==LV_KEY_DOWN)part=std::min<int>(detail["pages"].size()-1,part+1);else if(k==LV_KEY_ENTER){start_video();return;}else if(k=='f'){toggle_saved();return;}paint();return;}
    if(k=='w'||k=='s'||k==LV_KEY_UP||k==LV_KEY_DOWN){int delta=(k=='w'||k==LV_KEY_UP)?-1:1;navigate(Page((int(page)+6+delta)%6));return;}
    if(page==Page::Account){if(k=='r'||k==LV_KEY_ENTER)make_qr();return;}
    if(k=='j'||k=='k'){int count=std::min<int>(3,items.size()-offset);if(count>0)selected=(selected+count+(k=='j'?1:-1))%count;paint();}
    else if(k==LV_KEY_ENTER)open_detail(offset+selected);
    else if(k=='r')load_list();else if(k=='a'||k=='d'||k==LV_KEY_LEFT||k==LV_KEY_RIGHT){bool next=k=='d'||k==LV_KEY_RIGHT;
        if(next&&offset+3<int(items.size()))offset+=3;else if(!next&&offset>=3)offset-=3;else if(next&&more){++api_page;offset=selected=0;load_list();return;}else if(!next&&api_page>1){--api_page;offset=selected=0;load_list();return;}selected=0;paint();covers();}
    else if(k==LV_KEY_BACKSPACE&&(page==Page::Saved||page==Page::History)&&offset+selected<int(items.size())){auto&list=store[page==Page::Saved?"saved":"history"];list.erase(list.begin()+offset+selected);save_store();items=list;if(offset>=int(items.size()))offset=std::max(0,offset-3);selected=0;message("已从本机列表删除");paint();}
}
}
int main(int argc,char**argv){
    signal(SIGINT,signal_stop);signal(SIGTERM,signal_stop);signal(SIGPIPE,SIG_IGN);std::filesystem::create_directories(path("posters"));
    try{auto j=Json::parse(c1::read_file(path("session.json"),16384));if(j.is_object())api.cookies=j;}catch(...){}
    try{auto j=Json::parse(c1::read_file(path("library.json"),262144));for(auto k:{"saved","history"})if(j.contains(k)&&j[k].is_array())for(auto&v:j[k]){try{store[k].push_back(bili::summary(v));}catch(...){}if(store[k].size()==100)break;}}catch(...){}
    if(argc>1&&std::string(argv[1])=="--probe"){try{auto j=api.popular(1);std::cout<<"popular="<<j["items"].size()<<'\n';if(!j["items"].empty()){auto d=api.detail(j["items"][0]["bvid"]);auto s=api.stream(d["bvid"],d["pages"][0]["cid"]);std::cout<<"pages="<<d["pages"].size()<<" quality="<<s.quality<<" duration="<<s.duration<<'\n';}return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
    if(!screen::open())return 1;auto fp="A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf";font=lv_tiny_ttf_create_file(fp.c_str(),18);small=lv_tiny_ttf_create_file(fp.c_str(),16);large=lv_tiny_ttf_create_file(fp.c_str(),24);for(auto*f:{font,small,large})if(f)f->fallback=&lv_font_montserrat_18;
    paint();
    if(getenv("C1_BILI_QA_LOCAL")){detail={{"title","Bilibili / 原始 360p 静音验证"},{"bvid","BV14Dho6WE66"},{"pages",Json::array({{{"cid",1}}})}};page=Page::Detail;try{player.start({"/tmp/c1-bili-direct.mp4",20,16});playing=true;video_ui();}catch(const std::exception&e){message(e.what());}}
    else if(argc==3&&std::string(argv[1])=="--open"){items=Json::array({{{"bvid",argv[2]}}});open_detail(0);}else navigate(Page::Home);
    while(!screen::quit&&!interrupted){lv_timer_handler();for(uint32_t k;!screen::quit&&(k=screen::take_key());)key(k);
        if(busy&&job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto j=job.get();busy=false;auto done=std::move(complete);if(cancelled){c1::reset_requests();if(changing_video)abort_change("已取消切换");else message("已取消");}else if(j.value("ok",false)){try{done(j.at("data"));}catch(const std::exception&e){auto msg=std::string("响应处理失败 · ")+e.what();if(changing_video)abort_change(msg);else message(msg);}}else{auto msg=j.value("error","网络请求失败");if(changing_video)abort_change(msg);else message(msg);}}
        if(playing){player.poll();overlay();if(player.ended){player.ended=false;stop_video();}}
        if(!busy&&!qr_key.empty()&&page==Page::Account&&screen::tick()>=qr_next){if(screen::tick()>qr_expires){qr_key.clear();message("二维码已过期，按 R 刷新");}else{auto key=qr_key;qr_next=screen::tick()+3000;work("等待扫码确认…",[key]{return api.poll(key);},[](Json j){int code=j.value("code",-1);if(code==0){c1::save_private(path("session.json"),api.cookies.dump());qr_key.clear();qr_url.clear();navigate(Page::Account);}else if(code==86038){qr_key.clear();message("二维码已过期，按 R 刷新");}else if(code==86090)message("已扫码，请在手机上确认");else if(code==86101)message("等待扫码 · R 刷新");else message("登录状态异常："+std::to_string(code));});}}
        usleep(5000);
    }
    player.stop();c1::cancel_requests();if(job.valid())job.wait();if(audio)mixer_close(audio);lv_obj_clean(lv_screen_active());actions.clear();lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);for(auto*f:{font,small,large})if(f)lv_tiny_ttf_destroy(f);screen::close();return 0;
}
