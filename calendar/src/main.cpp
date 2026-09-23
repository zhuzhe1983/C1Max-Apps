#include "calendar_model.hpp"
#include "calendar_store.hpp"
#include "display.hpp"
#include "net.hpp"
#include "lvgl.h"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <unistd.h>

namespace {
using calendar::Date;
constexpr uint32_t bg=0x101923, panel=0x192633, ink=0xe5eef5, muted=0x91a4b5, accent=0x63d3b2;
const char *weekdays[]={"星期一","星期二","星期三","星期四","星期五","星期六","星期日"};
volatile sig_atomic_t interrupted=0;
lv_font_t *font=nullptr;
enum class Page { Month, Day, List, Detail, Edit, Manage, Sources, Builtins, SourceEdit, Delete, Discard, Sync };
Page page=Page::Month, detail_parent=Page::Day, edit_parent=Page::Month, confirm_parent=Page::Month;
Page list_parent=Page::Month, source_parent=Page::Manage, source_edit_parent=Page::Sources;
Date selected{}, month{};
calendar::Data data;
std::vector<calendar::Event> events, rows;
std::vector<calendar::DayInfo> grid;
calendar::Event current_event;
calendar::LocalEvent draft;
calendar::Source source_draft;
std::array<std::string,7> draft_text;
std::string original_form, directory, store_path, message, load_error, legacy_ics;
std::map<std::string,std::string> caches;
bool writable=true;
unsigned skipped=0;
struct Focus { lv_obj_t *obj; bool text; std::function<void()> action; };
std::vector<Focus> focus;
size_t focused=0;
lv_obj_t *footer=nullptr,*scroller=nullptr;
std::vector<lv_obj_t*> fields;
struct SyncResult { unsigned ok=0,failed=0;bool cancelled=false;std::string errors; };
std::future<SyncResult> job;
std::atomic<bool> cancel_sync{false};
std::atomic<unsigned> sync_done{0};
unsigned sync_total=0;
bool busy=false;
void render(); void back(); void refresh_events(); void show_edit(const std::string &id="");
void physical_key(uint32_t code);
void signal_handler(int){interrupted=1;}
void report(const std::string &text){message=text;if(footer)lv_label_set_text(footer,text.c_str());}
void safely(const std::function<void()> &fn){try{fn();}catch(const std::exception &e){report(e.what());}}
void set_focus(size_t index){
    if(focus.empty())return;
    if(focused<focus.size())lv_obj_remove_state(focus[focused].obj,LV_STATE_FOCUSED);
    focused=index%focus.size();lv_obj_add_state(focus[focused].obj,LV_STATE_FOCUSED);
    lv_obj_scroll_to_view(focus[focused].obj,LV_ANIM_OFF);
}
lv_obj_t *label(lv_obj_t *parent,const std::string &text,int x,int y,int width,uint32_t color=ink){
    auto o=lv_label_create(parent);lv_label_set_text(o,text.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);return o;
}
lv_obj_t *button(lv_obj_t *parent,const std::string &text,int x,int y,int width,int height,std::function<void()> fn){
    auto o=lv_button_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
    lv_obj_set_style_bg_color(o,lv_color_hex(panel),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(o,lv_color_hex(ink),0);lv_obj_set_style_radius(o,6,0);lv_obj_set_style_shadow_width(o,0,0);
    lv_obj_set_style_pad_all(o,4,0);lv_obj_set_style_border_width(o,0,0);
    lv_obj_set_style_outline_color(o,lv_color_hex(accent),LV_STATE_FOCUSED);lv_obj_set_style_outline_width(o,2,LV_STATE_FOCUSED);
    auto l=lv_label_create(o);lv_label_set_text(l,text.c_str());lv_obj_set_width(l,width-12);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_color(l,lv_color_hex(ink),0);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);lv_obj_set_height(l,26);lv_obj_center(l);
    auto index=focus.size();focus.push_back({o,false,std::move(fn)});
    lv_obj_add_event_cb(o,[](lv_event_t *e){auto i=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));if(i<focus.size()){auto action=focus[i].action;safely(action);}},LV_EVENT_CLICKED,reinterpret_cast<void*>(index));
    return o;
}
lv_obj_t *field(lv_obj_t *parent,const std::string &text,int x,int y,int width,int height,size_t maximum,bool multiline=false){
    auto o=lv_textarea_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,width,height);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x283e50),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_text_color(o,lv_color_hex(ink),0);lv_obj_set_style_border_width(o,1,0);
    lv_obj_set_style_border_color(o,lv_color_hex(0x415b70),0);lv_obj_set_style_border_color(o,lv_color_hex(accent),LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(o,2,LV_STATE_FOCUSED);lv_obj_set_style_pad_all(o,5,0);
    lv_textarea_set_one_line(o,!multiline);lv_textarea_set_max_length(o,maximum);lv_textarea_set_text(o,text.c_str());
    lv_textarea_set_cursor_pos(o,LV_TEXTAREA_CURSOR_LAST);
    auto index=focus.size();focus.push_back({o,true,{}});fields.push_back(o);
    lv_obj_add_event_cb(o,[](lv_event_t *e){set_focus(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));},LV_EVENT_CLICKED,reinterpret_cast<void*>(index));
    return o;
}
lv_obj_t *begin(const std::string &title,const std::string &hint,bool with_back=true){
    auto root=lv_screen_active();lv_obj_clean(root);focus.clear();fields.clear();focused=0;footer=nullptr;scroller=nullptr;
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(root,lv_color_hex(bg),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_set_style_text_color(root,lv_color_hex(ink),0);lv_obj_set_style_text_font(root,font,0);
    label(root,title,16,14,650);
    if(with_back)button(root,"返回",692,7,92,38,back);
    footer=label(root,message.empty()?hint:message,16,312,768,muted);lv_label_set_long_mode(footer,LV_LABEL_LONG_DOT);lv_obj_set_height(footer,24);
    return root;
}
lv_obj_t *scroll_area(lv_obj_t *root,int y=55,int height=246){
    auto o=lv_obj_create(root);lv_obj_remove_style_all(o);lv_obj_set_pos(o,16,y);lv_obj_set_size(o,768,height);
    lv_obj_set_scroll_dir(o,LV_DIR_VER);lv_obj_set_scrollbar_mode(o,LV_SCROLLBAR_MODE_AUTO);scroller=o;return o;
}
void load_files(){
    caches.clear();legacy_ics.clear();
    try{data=calendar::load_data(store_path);writable=true;load_error.clear();}
    catch(const std::exception&e){writable=false;load_error=e.what();message=load_error;}
    try{legacy_ics=calendar::read_bounded(directory+"/events.ics",256*1024);}
    catch(const std::exception&e){message=e.what();}
    for(const auto&s:data.sources)try{caches[s.id]=calendar::read_bounded(directory+"/cache-"+s.id+".ics",256*1024);}catch(const std::exception&e){message=e.what();}
}
void refresh_events(){
    events.clear();skipped=0;Date first{month.year,month.month,1};auto start=calendar::add_days(first,-calendar::weekday_monday0(first));auto end=calendar::add_days(start,41);
    for(const auto&e:data.events)if(e.start<=end&&start<=e.end)events.push_back(calendar::as_event(e));
    auto append=[&](const std::string &ics,const std::string &source){
        unsigned errors=0;auto list=calendar::parse_ics_events(ics,start,end,&errors);skipped+=errors;
        for(auto &e:list){if(events.size()>=1024){++skipped;break;}e.source_id=source;events.push_back(std::move(e));}
    };
    append(legacy_ics,"本地导入");
    for(const auto&s:data.sources)if(s.enabled)append(caches[s.id],s.name);
    std::sort(events.begin(),events.end(),[](const auto&a,const auto&b){if(a.start!=b.start)return a.start<b.start;if(a.time_text!=b.time_text)return a.time_text<b.time_text;return a.title<b.title;});
    grid=calendar::build_month_grid(month,selected,events);
}
void select_date(Date date){if(date.year<1900||date.year>2199)return;selected=date;month={date.year,date.month,1};refresh_events();page=Page::Month;render();}
void change_month(int delta){auto next=calendar::add_months(selected,delta);select_date(next);}
void commit(calendar::Data next){
    if(!writable)throw std::runtime_error("现有数据读取失败，已保护原文件，暂不能保存");
    calendar::save_data(store_path,next);data=std::move(next);refresh_events();
}
void export_local(){
    if(!writable)throw std::runtime_error("日历读取失败，未覆盖原导出文件");
    calendar::write_atomic(directory+"/export.ics",calendar::export_ics(data.events));
    report("已导出到 apps/data/calendar/export.ics");
}
void pull_fields(){
    if(page==Page::Edit&&fields.size()==7)for(size_t i=0;i<7;++i)draft_text[i]=lv_textarea_get_text(fields[i]);
    if(page==Page::SourceEdit&&fields.size()==2){source_draft.name=lv_textarea_get_text(fields[0]);source_draft.url=lv_textarea_get_text(fields[1]);}
}
std::string form_value(){
    std::string out;
    if(page==Page::Edit){for(const auto&s:draft_text)out+=std::to_string(s.size())+":"+s;out+=draft.all_day?'1':'0';}
    else {out=source_draft.name+"\n"+source_draft.url+(source_draft.enabled?"1":"0");}
    return out;
}
void show_edit(const std::string &id){
    if(!writable){report(load_error);return;}
    edit_parent=page;draft=calendar::LocalEvent{};draft.start=draft.end=selected;
    if(!id.empty()){auto it=std::find_if(data.events.begin(),data.events.end(),[&](const auto&e){return e.id==id;});if(it==data.events.end())return;draft=*it;}
    else if(data.events.size()>=256){report("最多保存 256 项本地日程");return;}
    draft_text={draft.title,calendar::date_key(draft.start),draft.start_time,calendar::date_key(draft.end),draft.end_time,draft.location,draft.note};
    page=Page::Edit;original_form=form_value();message.clear();render();
}
void save_event(){
    pull_fields();auto next=draft;next.title=draft_text[0];next.start_time=draft_text[2];next.end_time=draft_text[4];next.location=draft_text[5];next.note=draft_text[6];
    if(!calendar::parse_date_text(draft_text[1],&next.start)||!calendar::parse_date_text(draft_text[3],&next.end)){report("日期格式为 YYYY-MM-DD（1900–2199）");return;}
    auto error=calendar::validate(next);if(!error.empty()){report(error);return;}
    auto updated=data;if(next.id.empty()){next.id=calendar::new_id();updated.events.push_back(next);}else for(auto &e:updated.events)if(e.id==next.id)e=next;
    commit(std::move(updated));selected=next.start;month={selected.year,selected.month,1};refresh_events();current_event=calendar::as_event(next);
    if(edit_parent!=Page::Detail){detail_parent=edit_parent==Page::Month?Page::Day:edit_parent==Page::Manage?Page::List:edit_parent;if(edit_parent==Page::Manage)list_parent=Page::Manage;}
    page=Page::Detail;message="日程已保存";render();
}
void show_source(const std::string &id=""){
    if(!writable){report(load_error);return;}source_draft=calendar::Source{};
    if(!id.empty()){auto it=std::find_if(data.sources.begin(),data.sources.end(),[&](const auto&s){return s.id==id;});if(it==data.sources.end())return;source_draft=*it;}
    else if(data.sources.size()>=calendar::max_sources){report("最多添加 16 个 ICS 订阅");return;}
    source_edit_parent=Page::Sources;
    page=Page::SourceEdit;original_form=form_value();message.clear();render();
}
void show_builtin(const std::string &id){
    if(!writable){report(load_error);return;}
    source_draft=calendar::builtin_source_draft(data,id);source_edit_parent=Page::Builtins;
    page=Page::SourceEdit;original_form=form_value();message.clear();render();
}
bool source_exists(){
    return std::any_of(data.sources.begin(),data.sources.end(),[](const auto&s){return s.id==source_draft.id;});
}
void save_source(){
    pull_fields();source_draft.url=calendar::normalize_url(source_draft.url);auto error=calendar::validate(source_draft);if(!error.empty()){report(error);return;}
    auto updated=data;std::string drop_cache;
    if(source_draft.id.empty())source_draft.id=calendar::new_id();
    auto found=std::find_if(updated.sources.begin(),updated.sources.end(),[](const auto&s){return s.id==source_draft.id;});
    if(found==updated.sources.end())updated.sources.push_back(source_draft);
    else {if(found->url!=source_draft.url)drop_cache=found->id;*found=source_draft;}
    commit(std::move(updated));if(!drop_cache.empty()){caches.erase(drop_cache);unlink((directory+"/cache-"+drop_cache+".ics").c_str());refresh_events();}
    page=Page::Sources;message=source_draft.enabled?"订阅已启用；按 Y 同步":"订阅已保存并停用；启用后按 Y 同步";render();
}
void remove_item(){
    auto updated=data;
    if(confirm_parent==Page::SourceEdit){auto id=source_draft.id;updated.sources.erase(std::remove_if(updated.sources.begin(),updated.sources.end(),[&](const auto&s){return s.id==id;}),updated.sources.end());commit(std::move(updated));unlink((directory+"/cache-"+id+".ics").c_str());caches.erase(id);page=Page::Sources;}
    else {auto id=current_event.id;updated.events.erase(std::remove_if(updated.events.begin(),updated.events.end(),[&](const auto&e){return e.id==id;}),updated.events.end());commit(std::move(updated));page=detail_parent;}
    message="已删除";render();
}
void start_sync(){
    if(busy)return;auto sources=data.sources;sync_total=0;for(const auto&s:sources)sync_total+=s.enabled;
    if(!sync_total){report("请先添加并启用一个 ICS 订阅");return;}
    if(page==Page::Manage)source_parent=page;
    cancel_sync=false;sync_done=0;c1::reset_requests(8000);busy=true;page=Page::Sync;message.clear();auto path=directory;
    job=std::async(std::launch::async,[sources,path]{
        SyncResult result;
        for(const auto&s:sources){if(!s.enabled)continue;if(cancel_sync){result.cancelled=true;break;}
            try{auto reply=c1::http("GET",s.url);if(reply.status!=200)throw std::runtime_error("HTTP "+std::to_string(reply.status));
                if(reply.body.size()>256*1024||reply.body.find("BEGIN:VCALENDAR")==std::string::npos||reply.body.find("END:VCALENDAR")==std::string::npos)throw std::runtime_error("内容不是完整 ICS，或超过 256 KiB");
                if(cancel_sync){result.cancelled=true;break;}
                calendar::write_atomic(path+"/cache-"+s.id+".ics",reply.body);++result.ok;
            }catch(const std::exception&e){if(cancel_sync){result.cancelled=true;break;}++result.failed;if(result.errors.size()<600)result.errors+=s.name+": "+e.what()+"; ";}
            ++sync_done;
        }
        return result;
    });render();
}
void back(){
    if(busy){cancel_sync=true;c1::cancel_requests();report("正在取消同步，保留已有缓存");return;}
    if(page==Page::Edit||page==Page::SourceEdit){pull_fields();if(form_value()!=original_form){confirm_parent=page;page=Page::Discard;message.clear();render();return;}page=page==Page::Edit?edit_parent:source_edit_parent;}
    else if(page==Page::Discard||page==Page::Delete)page=confirm_parent;
    else if(page==Page::Detail)page=detail_parent;
    else if(page==Page::Sources)page=source_parent;
    else if(page==Page::Builtins)page=Page::Sources;
    else if(page==Page::List)page=list_parent;
    else page=Page::Month;
    message.clear();render();
}
void render(){
    if(page==Page::Month){
        auto root=begin("日历","WASD选日 Q/E翻月 N新增 L日程 I订阅 M管理",false);
        button(root,"Q <",150,7,58,38,[]{change_month(-1);});auto title=label(root,std::to_string(month.year)+"年 "+std::to_string(month.month)+"月",220,14,220);lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
        button(root,"> E",450,7,58,38,[]{change_month(1);});button(root,"T 今日",526,7,80,38,[]{select_date(calendar::today_local());});
        button(root,"N 新增",618,7,80,38,[]{show_edit();});button(root,"M 管理",708,7,80,38,[]{page=Page::Manage;message.clear();render();});
        const char*short_days[]={"一","二","三","四","五","六","日"};for(int col=0;col<7;++col){auto l=label(root,short_days[col],16+col*72,65,65,col>=5?accent:muted);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);}
        for(size_t i=0;i<grid.size();++i){const auto &d=grid[i];auto cell=button(root,std::to_string(d.date.day)+(d.event_count?" ·":""),16+int(i%7)*72,93+int(i/7)*34,65,30,[i]{select_date(grid[i].date);});
            lv_obj_set_style_bg_color(cell,lv_color_hex(d.selected?accent:d.today?0x294b44:panel),0);auto l=lv_obj_get_child(cell,0);lv_obj_set_style_text_color(l,lv_color_hex(d.selected?bg:d.in_month?ink:muted),0);}
        label(root,calendar::date_key(selected),540,65,244);label(root,weekdays[calendar::weekday_monday0(selected)],540,96,244,accent);
        auto chosen=calendar::events_for_date(events,selected);std::string preview=chosen.empty()?"这一天没有日程\n按 N 添加新日程":std::to_string(chosen.size())+" 项日程\n"+chosen.front().title;
        auto l=label(root,preview,540,133,244);lv_obj_set_height(l,97);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);
        button(root,"Enter 查看 / 编辑",540,249,244,44,[]{page=Page::Day;message.clear();render();});
        if(skipped)report("部分 ICS 规则或超额条目已跳过；本地日程可正常管理");return;
    }
    if(page==Page::Day||page==Page::List){
        auto root=begin(page==Page::Day?calendar::date_key(selected)+" 的日程":"全部本地日程","W/S选择 Enter查看 N新增 中间返回退一层");
        button(root,"N 新增",582,7,96,38,[]{show_edit();});auto list=scroll_area(root);rows.clear();
        if(page==Page::Day)rows=calendar::events_for_date(events,selected);else {for(const auto&e:data.events)rows.push_back(calendar::as_event(e));std::sort(rows.begin(),rows.end(),[](const auto&a,const auto&b){if(a.start!=b.start)return a.start<b.start;return a.time_text<b.time_text;});}
        if(rows.empty())label(list,"没有日程。按 N 或轻点“新增”建立第一项。",6,30,735,muted);
        for(size_t i=0;i<rows.size();++i){const auto&e=rows[i];std::string title=calendar::date_key(e.start)+"  "+(e.all_day?"全天":e.time_text)+"  "+e.title+(e.local?"":"  [订阅]");
            button(list,title,0,int(i)*49,748,42,[i]{current_event=rows.at(i);detail_parent=page;page=Page::Detail;message.clear();render();});}
        set_focus(focus.size()>2?2:1);return;
    }
    if(page==Page::Detail){
        auto root=begin(current_event.local?"本地日程":"订阅日程（只读）",current_event.local?"W/S滚动 E编辑 D删除 中间返回退一层":"W/S滚动；订阅只读，中间返回退一层");auto body=scroll_area(root,58,190);
        std::string text=current_event.title+"\n"+calendar::date_key(current_event.start)+" — "+calendar::date_key(current_event.end)+"\n"+(current_event.all_day?"全天":current_event.time_text);
        if(!current_event.location.empty())text+="\n地点："+current_event.location;if(!current_event.description.empty())text+="\n备注："+current_event.description;if(!current_event.local)text+="\n来源："+current_event.source_id;
        label(body,text,0,0,738);
        if(current_event.local){button(root,"E 编辑",16,260,180,42,[]{show_edit(current_event.id);});button(root,"D 删除",214,260,180,42,[]{confirm_parent=Page::Detail;page=Page::Delete;message.clear();render();});}
        else label(root,"通过原订阅服务修改；同步后更新。",16,268,748,muted);set_focus(focus.size()>1?1:0);return;
    }
    if(page==Page::Edit){
        auto root=begin(draft.id.empty()?"新增日程":"编辑日程",std::string(screen::caps_lock()?"ABC":"abc")+"  Enter下一项/确认；按钮W/S切换；返回取消",false);
        label(root,"标题",16,52,200,muted);field(root,draft_text[0],16,76,768,38,256);
        label(root,"开始日期",16,121,170,muted);label(root,"开始时间",197,121,125,muted);label(root,"结束日期",338,121,170,muted);label(root,"结束时间",520,121,125,muted);
        field(root,draft_text[1],16,144,168,38,10);field(root,draft_text[2],197,144,120,38,5);field(root,draft_text[3],338,144,168,38,10);field(root,draft_text[4],520,144,120,38,5);
        label(root,"地点",16,190,200,muted);field(root,draft_text[5],16,214,255,70,512,true);label(root,"备注",289,190,200,muted);field(root,draft_text[6],289,214,495,70,4096,true);
        button(root,draft.all_day?"全天 ✓":"全天 ○",658,144,126,38,[]{pull_fields();draft.all_day=!draft.all_day;render();set_focus(7);});
        button(root,"保存",570,7,100,38,save_event);button(root,"取消",684,7,100,38,back);set_focus(0);return;
    }
    if(page==Page::Manage){
        auto root=begin("日程管理","W/S选择 Enter确认 中间返回月历");
        button(root,"N  新增本地日程",16,58,370,49,[]{show_edit();});button(root,"L  全部本地日程 ("+std::to_string(data.events.size())+")",406,58,378,49,[]{list_parent=page;page=Page::List;message.clear();render();});
        button(root,"I  ICS 订阅管理 ("+std::to_string(data.sources.size())+")",16,116,370,49,[]{source_parent=page;page=Page::Sources;message.clear();render();});
        button(root,"B  内置外部日历",406,116,378,49,[]{source_parent=page;page=Page::Builtins;message.clear();render();});
        button(root,"Y  同步已启用订阅",16,174,370,49,start_sync);
        button(root,"R  重新读取本地文件",406,174,378,49,[]{load_files();refresh_events();message="已重新读取日历与缓存";render();});
        button(root,"X  导出本地日程为 ICS",16,232,370,49,export_local);set_focus(1);return;
    }
    if(page==Page::Sources){
        auto root=begin("ICS 订阅管理","W/S选择 Enter编辑 B内置 N新增 Y同步");
        button(root,"B 内置",360,7,97,38,[]{page=Page::Builtins;message.clear();render();});
        button(root,"N 新增",469,7,97,38,[]{show_source();});button(root,"Y 同步",578,7,98,38,start_sync);auto list=scroll_area(root);
        if(data.sources.empty())label(list,"按 B 选择内置节假日 / 黄历，或按 N 添加 ICS / webcal。",0,24,740,muted);
        for(size_t i=0;i<data.sources.size();++i){const auto&s=data.sources[i];button(list,std::string(s.enabled?"[启用] ":"[停用] ")+s.name+(caches[s.id].empty()?" · 无缓存":" · 有缓存"),0,int(i)*51,748,44,[i]{show_source(data.sources.at(i).id);});}
        set_focus(focus.size()>4?4:1);return;
    }
    if(page==Page::Builtins){
        auto root=begin("内置外部日历","W/S选择 Enter设置；Y同步；中间返回订阅");
        label(root,"沿用 CardputerZero 来源；默认停用，启用并保存后按 Y 同步。",16,55,768,muted);
        const auto &presets=calendar::builtin_sources();
        for(size_t i=0;i<presets.size();++i){const auto &preset=presets[i];auto *saved=calendar::find_builtin_source(data,preset);
            auto state=saved?(saved->enabled?" · 已启用":" · 已停用"):" · 未添加";
            button(root,preset.name+state,16+int(i%2)*390,94+int(i/2)*52,378,44,[i]{show_builtin(calendar::builtin_sources().at(i).id);});}
        set_focus(1);return;
    }
    if(page==Page::SourceEdit){
        auto root=begin(source_exists()?"编辑 ICS 订阅":"新增 ICS 订阅",std::string(screen::caps_lock()?"ABC":"abc")+"  Enter下一项/确认；按钮W/S切换；返回取消",false);
        label(root,"名称",16,61,250,muted);field(root,source_draft.name,16,90,768,42,128);
        label(root,"ICS / webcal 地址（保存在设备私有文件中）",16,147,760,muted);field(root,source_draft.url,16,176,768,48,2048);
        button(root,source_draft.enabled?"订阅：启用":"订阅：停用",16,250,230,44,[]{pull_fields();source_draft.enabled=!source_draft.enabled;render();set_focus(2);});
        button(root,"保存",570,7,100,38,save_source);button(root,"取消",684,7,100,38,back);
        if(source_exists())button(root,"删除订阅",267,250,200,44,[]{pull_fields();confirm_parent=Page::SourceEdit;page=Page::Delete;message.clear();render();});set_focus(0);return;
    }
    if(page==Page::Delete||page==Page::Discard){
        auto root=begin(page==Page::Delete?"确认删除":"未保存的修改","W/S切换 Enter确认 中间返回继续编辑",false);
        label(root,page==Page::Delete?"删除这项内容？此操作不会修改其他日程或原订阅服务。":"离开将放弃此次未保存的修改。",32,95,730);
        button(root,"取消",100,211,260,60,back);
        button(root,page==Page::Delete?"确认删除":"放弃修改",424,211,260,60,[]{if(page==Page::Delete)remove_item();else {page=confirm_parent==Page::Edit?edit_parent:source_edit_parent;message.clear();render();}});set_focus(0);return;
    }
    auto root=begin("正在同步 ICS 订阅","返回键取消同步；关机键返回 launcher",false);
    label(root,"逐个更新启用的订阅。下载失败会保留旧缓存。",20,104,750);
    button(root,"取消同步",250,215,300,58,back);set_focus(0);
}
void physical_key(uint32_t code){
    if(code==screen::KEY_HOME){cancel_sync=true;c1::cancel_requests();screen::quit=true;return;}
    if(code==screen::KEY_EXIT||code==LV_KEY_ESC){back();return;}
    if(code==screen::KEY_MODE){report(std::string(screen::caps_lock()?"ABC 大写":"abc 小写")+"；Enter下一项，返回取消");return;}
    if(busy){if(code==LV_KEY_ENTER)back();return;}
    const bool editor=page==Page::Edit||page==Page::SourceEdit;
    if(editor&&focused<focus.size()&&focus[focused].text){
        auto o=focus[focused].obj;
        if(code==LV_KEY_ENTER||code==LV_KEY_NEXT||code=='\t'){pull_fields();set_focus(focused+1);return;}
        if(code==LV_KEY_BACKSPACE){lv_textarea_delete_char(o);return;}
        if(code==LV_KEY_LEFT){lv_textarea_cursor_left(o);return;}if(code==LV_KEY_RIGHT){lv_textarea_cursor_right(o);return;}
        if(code>=32&&code<0x10000&&code!=127){char value[4]={0};if(code<128)value[0]=char(code);else if(code<2048){value[0]=char(0xc0|(code>>6));value[1]=char(0x80|(code&63));}else {value[0]=char(0xe0|(code>>12));value[1]=char(0x80|((code>>6)&63));value[2]=char(0x80|(code&63));}lv_textarea_add_text(o,value);}return;
    }
    if(code>='A'&&code<='Z')code+='a'-'A';
    if(page==Page::Month){int delta=code=='a'||code==LV_KEY_LEFT?-1:code=='d'||code==LV_KEY_RIGHT?1:code=='w'||code==LV_KEY_UP?-7:code=='s'||code==LV_KEY_DOWN?7:0;
        if(delta){select_date(calendar::add_days(selected,delta));return;}if(code=='q'){change_month(-1);return;}if(code=='e'){change_month(1);return;}if(code=='t'){select_date(calendar::today_local());return;}
        if(code==LV_KEY_ENTER){page=Page::Day;message.clear();render();return;}}
    if(!editor){
        if(page==Page::Detail&&(code=='w'||code=='s'||code==LV_KEY_UP||code==LV_KEY_DOWN)){
            lv_obj_scroll_by_bounded(scroller,0,(code=='w'||code==LV_KEY_UP)?54:-54,LV_ANIM_OFF);return;
        }
        if(code=='n'&&(page==Page::Month||page==Page::Day||page==Page::List||page==Page::Manage)){show_edit();return;}
        if(code=='n'&&page==Page::Sources){show_source();return;}
        if(code=='l'&&(page==Page::Month||page==Page::Manage)){list_parent=page;page=Page::List;message.clear();render();return;}
        if(code=='i'&&(page==Page::Month||page==Page::Manage)){source_parent=page;page=Page::Sources;message.clear();render();return;}
        if(code=='b'&&(page==Page::Sources||page==Page::Manage)){if(page==Page::Manage)source_parent=page;page=Page::Builtins;message.clear();render();return;}
        if(code=='m'&&page==Page::Month){page=Page::Manage;message.clear();render();return;}
        if(code=='y'&&(page==Page::Manage||page==Page::Sources||page==Page::Builtins)){start_sync();return;}
        if(code=='r'&&(page==Page::Month||page==Page::Manage)){load_files();refresh_events();message="已重新读取文件";render();return;}
        if(code=='x'&&page==Page::Manage){export_local();return;}
        if(page==Page::Detail&&current_event.local){if(code=='e'){show_edit(current_event.id);return;}if(code=='d'){confirm_parent=Page::Detail;page=Page::Delete;message.clear();render();return;}}
    }
    if(code==LV_KEY_ENTER||code==' '){if(focused<focus.size()&&!focus[focused].text){auto action=focus[focused].action;action();}return;}
    if(code=='s'||code=='j'||code==LV_KEY_DOWN||code==LV_KEY_NEXT||code=='\t')set_focus(focused+1);
    else if(code=='w'||code=='k'||code==LV_KEY_UP)set_focus(focused+focus.size()-1);
}
}
int main(int argc,char **argv){
    unsigned duration=0;
    if(argc==2&&!strcmp(argv[1],"--version")){puts("C1Max Calendar 0.3.0");return 0;}
    if(argc==3&&!strcmp(argv[1],"--smoke-ms")){char *end=nullptr;auto n=strtoul(argv[2],&end,10);if(*end||n<1||n>60000)return 2;duration=unsigned(n);}
    else if(argc!=1){fprintf(stderr,"Usage: %s [--version | --smoke-ms 1..60000]\n",argv[0]);return 2;}
    signal(SIGINT,signal_handler);signal(SIGTERM,signal_handler);signal(SIGPIPE,SIG_IGN);
    directory=c1::data()+"/calendar";store_path=directory+"/calendar.db";selected=calendar::today_local();month={selected.year,selected.month,1};load_files();refresh_events();
    if(!screen::open()){screen::close();return 1;}
    font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),18);
    if(!font){fprintf(stderr,"Calendar: required Chinese font is missing\n");screen::close();return 1;}
    render();auto started=screen::tick(),last_tick=started;auto today=calendar::today_local();
    while(!screen::quit&&!interrupted){
        lv_timer_handler();for(uint32_t code;!screen::quit&&!interrupted&&(code=screen::take_key())!=0;)safely([&]{physical_key(code);});
        if(busy&&job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            auto result=job.get();busy=false;load_files();refresh_events();page=Page::Sources;
            message=(result.cancelled?"已取消；":"")+std::string("同步成功 ")+std::to_string(result.ok)+"，失败 "+std::to_string(result.failed);
            if(!result.errors.empty())message+="；"+result.errors;render();
        }
        auto now=screen::tick();if(duration&&now-started>=duration)break;
        if(now-last_tick>=1000){last_tick=now;if(busy)report("已处理 "+std::to_string(sync_done.load())+" / "+std::to_string(sync_total)+" 个来源");
            auto date=calendar::today_local();if(date!=today){today=date;if(page==Page::Month){refresh_events();render();}}}
        usleep(10000);
    }
    cancel_sync=true;c1::cancel_requests();if(job.valid())job.wait();
    lv_obj_clean(lv_screen_active());focus.clear();lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);lv_tiny_ttf_destroy(font);screen::close();return 0;
}
