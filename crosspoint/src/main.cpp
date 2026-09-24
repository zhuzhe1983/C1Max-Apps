#include "display.hpp"
#include "net.hpp"
#include "default_servers.hpp"
#include "reader.hpp"
#include "opds.hpp"
#include "transfer.hpp"
#include "paginate.hpp"
#include "epub.hpp"
#include "lv_tiny_ttf.h"
#include <lvgl.h>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <dirent.h>
#include <functional>
#include <memory>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {
using crosspoint::Server;
enum class View { Library, Reader, Catalog, Book, Settings, Search, Busy, Error };
enum class Navigation { Root, Forward, Back, Refresh };
enum class Job { None, Feed, Download, OpenEpub, Chapter };
constexpr uint32_t ink=0x263c38,paper=0xf3eddc,teal=0x345f59,muted=0x748078,copper=0xb07d52;
constexpr int rows=6;
View view=View::Library,return_view=View::Library;
std::vector<Server> servers;
std::vector<std::string> books,pages;
std::string reader_text;size_t reading_end=0,page_origin=0;
std::unique_ptr<crosspoint::EpubBook> epub,result_epub;
size_t chapter_index=0,result_chapter=0;
std::string result_text;
int book_index=0,entry_index=0,format_index=0,page=0,field=0;
crosspoint::Feed feed;
struct Location {std::string url;int index=0;};
std::vector<Location> history;
Server settings_draft;bool have_settings_draft=false;
lv_font_t *font=nullptr,*reader_font=nullptr;
int reader_size=18;
void* font_map=MAP_FAILED;size_t font_map_size=0;
lv_obj_t *hint=nullptr,*fields[4]{},*query_field=nullptr,*progress_label=nullptr,*progress_bar=nullptr;
std::vector<std::function<void()>> actions;
std::function<void()> deferred;
std::string reader_title,search_query,error_text;
volatile sig_atomic_t stopped=0;
Job job=Job::None;Navigation pending_navigation=Navigation::Root;
std::thread worker;
std::atomic<bool> cancelled{false},done{false};
crosspoint::Progress progress;
crosspoint::Feed result_feed;
std::string result_error,result_file;
uint32_t progress_at=0;
uint32_t job_started_at=0;
std::string last_progress;
void library();void catalog();void details();void settings();void search();void render_return();
void begin_feed(const std::string&,Navigation);void open_entry();void open_book();void download();void key(uint32_t);
void busy(const std::string&,View);void load_chapter(int direction);
void signal_stop(int){stopped=1;}
struct FontShortcuts {
    FontShortcuts(){int fd=open("/tmp/c1max-crosspoint-font.pid",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd>=0){dprintf(fd,"%ld\n",(long)getpid());close(fd);}}
    ~FontShortcuts(){unlink("/tmp/c1max-crosspoint-font.pid");}
};
std::string data_dir(){return c1::data()+"/crosspoint";}
std::string book_dir(){return data_dir()+"/books";}
std::string draft_path(){return data_dir()+"/settings-draft.json";}
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");return a==std::string::npos?"":s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
std::string filename(const std::string& s){auto p=s.find_last_of('/');return p==std::string::npos?s:s.substr(p+1);}
std::string size_text(uint64_t n){char s[40];if(n>=1024*1024)snprintf(s,sizeof(s),"%.1f MiB",n/(1024.0*1024));else snprintf(s,sizeof(s),"%llu KiB",(unsigned long long)((n+1023)/1024));return s;}
lv_obj_t* label(const std::string& s,int x,int y,int w,uint32_t color=ink){
    auto* o=lv_label_create(lv_screen_active());lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,22);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);if(font)lv_obj_set_style_text_font(o,font,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_obj_remove_flag(o,LV_OBJ_FLAG_CLICKABLE);return o;
}
lv_obj_t* panel(int x,int y,int w,int h,uint32_t color=paper){
    auto* o=lv_obj_create(lv_screen_active());lv_obj_remove_style_all(o);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,8,0);return o;
}
void clickable(lv_obj_t* o,std::function<void()> action){
    auto id=actions.size();actions.push_back(std::move(action));lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(o,[](lv_event_t* e){auto id=reinterpret_cast<uintptr_t>(lv_event_get_user_data(e));if(id<actions.size())deferred=actions[id];},LV_EVENT_CLICKED,reinterpret_cast<void*>(id));
}
void button(const std::string& s,int x,int y,int w,int h,std::function<void()> action,bool enabled=true){
    auto* o=panel(x,y,w,h,enabled?teal:0xd9d9cb);if(enabled)clickable(o,std::move(action));
    auto* t=label(s,x,y+(h-20)/2,w,enabled?0xfaf6eb:muted);lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_CENTER,0);
}
void frame(const std::string& title){
    lv_obj_clean(lv_screen_active());actions.clear();hint=nullptr;query_field=nullptr;progress_label=progress_bar=nullptr;
    for(auto& f:fields)f=nullptr;
    auto* root=lv_screen_active();lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(0xe9e2d0),0);if(font)lv_obj_set_style_text_font(root,font,0);
    label("CROSS / POINT",16,9,204);label(title,230,9,554,muted);
}
void footer(const std::string& s){hint=label(s,16,308,770,muted);}
void note(const std::string& s){if(hint)lv_label_set_text(hint,s.c_str());}
void error(const std::string& message,View back){
    return_view=back;view=View::Error;error_text=message;frame("暂时无法完成");panel(16,47,768,244);
    auto* t=label(message,36,74,720,0x8b4b3c);lv_label_set_long_mode(t,LV_LABEL_LONG_WRAP);lv_obj_set_height(t,126);
    button("返回",36,230,128,44,[]{render_return();});footer("返回键返回上一页；检查网络或服务器设置后重试");
}
void scan(){
    books.clear();DIR* d=opendir(book_dir().c_str());if(!d)return;
    while(auto* e=readdir(d)){std::string n=e->d_name;if(n.empty()||n[0]=='.')continue;
        struct stat st{};if(lstat((book_dir()+"/"+n).c_str(),&st)||!S_ISREG(st.st_mode))continue;
        auto p=n.find_last_of('.');if(p==std::string::npos)continue;auto ext=n.substr(p);for(char& c:ext)c=char(tolower((unsigned char)c));
        if(ext==".epub"||ext==".pdf"||ext==".txt"||ext==".md"||ext==".markdown"||ext==".azw3"||ext==".azw"||ext==".mobi"||ext==".prc")books.push_back(n);
    }closedir(d);std::sort(books.begin(),books.end());book_index=std::clamp(book_index,0,std::max(0,int(books.size())-1));
}
void load_servers(){
    try{auto j=Json::parse(c1::read_file(data_dir()+"/servers.json",65536));for(auto& x:j){Server s{x.value("name",std::string()),x.value("url",std::string()),x.value("user",std::string()),x.value("password",std::string())};if(!s.url.empty()&&servers.size()<8)servers.push_back(s);}}catch(...){}
    if(servers.empty()){auto d=c1::default_server("crosspoint");if(!d.empty())servers.push_back({d.value("name",std::string()),d.value("url",std::string()),d.value("user",std::string()),d.value("password",std::string())});}
}
void save_servers(){Json j=Json::array();for(auto& s:servers)j.push_back({{"name",s.name},{"url",s.url},{"user",s.user},{"password",s.password}});c1::save_private(data_dir()+"/servers.json",j.dump(2)+"\n");}
void load_draft(){try{auto j=Json::parse(c1::read_file(draft_path(),8192));settings_draft={j.value("name",std::string()),j.value("url",std::string()),j.value("user",std::string()),j.value("password",std::string())};have_settings_draft=true;}catch(...){} }
void save_draft(){
    if(!fields[0]||!fields[3])return;
    settings_draft={lv_textarea_get_text(fields[0]),lv_textarea_get_text(fields[1]),lv_textarea_get_text(fields[2]),lv_textarea_get_text(fields[3])};
    try{c1::save_private(draft_path(),Json{{"name",settings_draft.name},{"url",settings_draft.url},{"user",settings_draft.user},{"password",settings_draft.password}}.dump()+"\n");have_settings_draft=true;}catch(const std::exception& e){note(e.what());}
}
void focus_field(int index,bool cursor=false){
    field=std::clamp(index,0,3);for(int i=0;i<4;++i)if(fields[i]){lv_obj_clear_state(fields[i],LV_STATE_FOCUSED);lv_obj_set_style_border_color(fields[i],lv_color_hex(i==field?copper:0xd6cdb8),0);}
    if(fields[field]){lv_obj_add_state(fields[field],LV_STATE_FOCUSED);if(cursor)lv_textarea_set_cursor_pos(fields[field],LV_TEXTAREA_CURSOR_LAST);}
}
void show_page(){
    view=View::Reader;frame(reader_title);panel(12,42,776,258,0xfaf6eb);
    page=std::clamp(page,0,std::max(0,int(pages.size())-1));auto* body=label(pages.empty()?"Empty document":pages[page],28,54,742);
    if(reader_font)lv_obj_set_style_text_font(body,reader_font,0);
    lv_obj_set_height(body,226);lv_label_set_long_mode(body,LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(body,0,0);lv_obj_set_style_text_letter_space(body,0,0);
    auto position=epub?"章 "+std::to_string(chapter_index+1)+"/"+std::to_string(epub->chapter_count())+"  ·  W/S 换章  ·  ":"";
    size_t start=page_origin;for(int i=0;i<page;++i)start+=pages[i].size();
    auto location=page_origin?"进度 "+std::to_string(start*100/std::max<size_t>(1,reader_text.size()))+"%":"第 "+std::to_string(page+1)+" 页";
    footer(position+"A/D 翻页  ·  "+location+(!epub&&!page_origin&&reading_end>=reader_text.size()?" / 共 "+std::to_string(pages.size())+" 页":"")+"  ·  返回：书架");
}
bool page_fits(const std::string& candidate){
    lv_point_t extent{};lv_text_get_size(&extent,candidate.c_str(),reader_font?reader_font:font?font:LV_FONT_DEFAULT,0,0,742,LV_TEXT_FLAG_NONE);
    return extent.y<=226;
}
void append_page(){
    auto next=crosspoint::next_page(reader_text,reading_end,page_fits);reading_end+=next.size();pages.push_back(std::move(next));
}
void resize_reader(int delta){
    int next=std::clamp(reader_size+delta,14,32);if(!reader_font||next==reader_size)return;
    size_t anchor=page_origin;for(int i=0;i<page;++i)anchor+=pages[i].size();
    reader_size=next;lv_tiny_ttf_set_size(reader_font,reader_size);
    // Keep the first character in view without reflowing the whole book.
    // Left lazily lays out the preceding page after a size change.
    page_origin=reading_end=anchor;pages.clear();page=0;append_page();show_page();
    try{c1::save_private(data_dir()+"/reader.json",Json{{"font_size",reader_size}}.dump()+"\n");}catch(const std::exception& e){note(e.what());}
    fprintf(stderr,"[crosspoint] font=%d chapter=%zu anchor=%zu end=%zu\n",reader_size,chapter_index+1,anchor,reading_end);
}
void open_book(){
    if(books.empty()||job!=Job::None)return;std::string text,why;reader_title=books[book_index];
    auto path=book_dir()+"/"+reader_title;auto ext=path.substr(path.find_last_of('.'));for(char& c:ext)c=char(tolower((unsigned char)c));
    if(ext==".epub"){
        cancelled=false;done=false;result_error.clear();result_file.clear();result_text.clear();result_epub.reset();
        job=Job::OpenEpub;busy("正在打开 EPUB…",View::Library);
        worker=std::thread([path]{try{
            result_epub=std::make_unique<crosspoint::EpubBook>();result_epub->open(path,cancelled);
            result_chapter=0;
            do{result_text=result_epub->chapter(result_chapter,cancelled);if(!result_text.empty())break;}while(++result_chapter<result_epub->chapter_count());
            if(result_text.empty())throw std::runtime_error("EPUB contains no readable text");
        }catch(const std::exception& e){result_error=e.what();}done=true;});return;
    }
    epub.reset();
    note("正在打开图书…");lv_refr_now(nullptr);
    if(!crosspoint::read_book(book_dir()+"/"+reader_title,text,why)){error(why,View::Library);return;}
    reader_text=std::move(text);pages.clear();page_origin=reading_end=0;append_page();page=0;show_page();
}
void load_chapter(int direction){
    if(!epub||job!=Job::None)return;
    if((direction<0&&chapter_index==0)||(direction>0&&chapter_index+1>=epub->chapter_count()))return;
    cancelled=false;done=false;result_error.clear();result_file.clear();result_text.clear();job=Job::Chapter;
    busy(direction>0?"正在读取下一章…":"正在读取上一章…",View::Reader);
    worker=std::thread([direction]{try{
        auto next=int(chapter_index)+direction;
        while(next>=0&&next<int(epub->chapter_count())){
            result_text=epub->chapter(size_t(next),cancelled);if(!result_text.empty()){result_chapter=size_t(next);break;}next+=direction;
        }
        if(result_text.empty())throw std::runtime_error("No more readable chapters");
    }catch(const std::exception& e){result_error=e.what();}done=true;});
}
void library(){
    view=View::Library;frame("本地书架");panel(14,43,512,254);panel(538,43,248,254,teal);
    label("已下载  ·  "+std::to_string(books.size())+" 本",28,52,480,muted);
    int first=(book_index/rows)*rows;
    for(int i=first;i<std::min(first+rows,int(books.size()));++i){int y=84+(i-first)*34;auto* row=panel(23,y,494,32,i==book_index?0xd9e4d7:paper);
        clickable(row,[i]{book_index=i;open_book();});label(books[i],33,y+5,474);
    }
    if(books.empty())label("暂无图书，打开在线书库下载",30,132,476,muted);
    label("OPDS / CALIBRE",556,60,214,0xf3eddc);
    label(servers.empty()?"尚未设置服务器":servers[0].name.empty()?"在线书库":servers[0].name,556,94,212,0xdde8db);
    button("在线书库  O",554,137,216,44,[]{if(servers.empty())settings();else if(feed.url.empty())begin_feed(servers[0].url,Navigation::Root);else catalog();});
    button("服务器设置  S",554,193,216,44,[]{field=0;settings();});
    label("EPUB · AZW3 · PDF · TXT",554,257,218,0xdde8db);
    footer("↑↓ / W / X 选择  ·  A / D 翻页  ·  回车阅读  ·  R 刷新");
}
void change_catalog_page(int direction){
    int first=(entry_index/rows)*rows;
    if(direction>0&&first+rows<int(feed.items.size())){entry_index=first+rows;catalog();}
    else if(direction<0&&first>0){entry_index=std::max(0,first-rows);catalog();}
    else {auto target=direction>0?feed.next:feed.previous;if(!target.empty())begin_feed(target,Navigation::Forward);}
}
void catalog(){
    view=View::Catalog;frame("在线书库");panel(14,43,772,254);
    label(feed.title.empty()?"OPDS":feed.title,28,53,430,muted);
    button("搜索",470,46,72,32,[]{search();},!feed.search.empty());
    button("上页",550,46,72,32,[]{change_catalog_page(-1);},entry_index>=rows||!feed.previous.empty());
    button("下页",630,46,72,32,[]{change_catalog_page(1);},(entry_index/rows+1)*rows<int(feed.items.size())||!feed.next.empty());
    button("书架",710,46,66,32,[]{library();});
    entry_index=std::clamp(entry_index,0,std::max(0,int(feed.items.size())-1));int first=(entry_index/rows)*rows;
    for(int i=first;i<std::min(first+rows,int(feed.items.size()));++i){int y=84+(i-first)*34;auto& e=feed.items[i];
        auto* row=panel(23,y,754,32,i==entry_index?0xd9e4d7:paper);clickable(row,[i]{entry_index=i;open_entry();});
        label((e.navigation()?"›  ":"↓  ")+e.title,33,y+5,642);
        std::string type=e.navigation()?"目录":e.formats.size()>1?std::to_string(e.formats.size())+" 格式":e.formats[0].extension.substr(1);
        auto* t=label(type,683,y+5,83,muted);lv_obj_set_style_text_align(t,LV_TEXT_ALIGN_RIGHT,0);
    }
    if(feed.items.empty())label("没有找到图书，试试其他关键词",32,137,700,muted);
    auto count=feed.items.empty()?"0":std::to_string(first+1)+"–"+std::to_string(std::min(first+rows,int(feed.items.size())));
    footer(count+" / "+std::to_string(feed.items.size())+"  ·  W/S 选择  A/D 翻页  F 搜索  回车打开  返回上级");
}
void open_entry(){if(feed.items.empty())return;if(feed.items[entry_index].navigation())begin_feed(feed.items[entry_index].href,Navigation::Forward);else{format_index=0;details();}}
void details(){
    if(feed.items.empty()){catalog();return;}view=View::Book;auto& b=feed.items[entry_index];frame("选择下载格式");panel(14,43,772,254);
    label(b.title,30,54,738);label(b.author.empty()?"作者未提供":b.author,30,83,738,muted);
    int first=(format_index/4)*4;
    for(int i=first;i<std::min(first+4,int(b.formats.size()));++i){int y=117+(i-first)*36;auto& f=b.formats[i];
        auto* row=panel(24,y,752,34,i==format_index?0xd9e4d7:paper);clickable(row,[i]{format_index=i;download();});
        label(f.extension.substr(1)+(f.size?"  ·  "+size_text(f.size):""),36,y+6,716);
    }
    label(b.formats[format_index].extension==".pdf"?"PDF 当前提取文字阅读；扫描版和漫画可能没有可提取文字":"下载后保存在本地书架，可离线阅读",30,273,740,muted);
    footer("W/S 选择格式  ·  回车下载  ·  返回：目录");
}
void busy(const std::string& title,View back){
    job_started_at=screen::tick();last_progress.clear();progress_at=0;
    return_view=back;view=View::Busy;frame(title);panel(16,47,768,246);
    label(title,36,80,720);progress_label=label("正在连接服务器…",36,125,720,muted);
    progress_bar=lv_bar_create(lv_screen_active());lv_obj_set_pos(progress_bar,36,172);lv_obj_set_size(progress_bar,726,8);lv_bar_set_range(progress_bar,0,100);
    button("取消",36,221,132,46,[]{cancelled=true;note("正在取消…");});footer("返回键取消；加载期间界面仍可响应");
}
void begin_feed(const std::string& url,Navigation nav){
    if(job!=Job::None||servers.empty())return;
    try{crosspoint::url_origin(url);}catch(const std::exception& e){error(e.what(),view);return;}
    auto back=view==View::Search?View::Search:feed.url.empty()?View::Library:View::Catalog;
    auto server=servers[0];pending_navigation=nav;cancelled=false;done=false;result_error.clear();result_file.clear();progress.bytes=0;progress.total=0;
    job=Job::Feed;busy("正在加载目录…",back);
    worker=std::thread([url,server]{try{auto d=crosspoint::fetch_document(url,server,cancelled,progress);if(!crosspoint::parse_feed(d.body,d.url,result_feed,result_error)){} }catch(const std::exception& e){result_error=e.what();}done=true;});
}
void download(){
    if(job!=Job::None||servers.empty()||feed.items.empty())return;
    auto b=feed.items[entry_index];auto f=b.formats[format_index];auto s=servers[0];auto dir=book_dir();
    cancelled=false;done=false;result_error.clear();result_file.clear();progress.bytes=0;progress.total=std::min<uint64_t>(f.size,UINT32_MAX);job=Job::Download;
    busy("正在下载："+b.title,View::Book);
    worker=std::thread([b,f,s,dir]{try{result_file=crosspoint::download_book(b,f,s,dir,cancelled,progress);}catch(const std::exception& e){result_error=e.what();}done=true;});
}
void render_return(){
    auto back=return_view;if(back==View::Reader)show_page();else if(back==View::Book)details();else if(back==View::Catalog)catalog();else if(back==View::Search)search();else if(back==View::Settings)settings();else library();
}
void poll_job(){
    if(job==Job::None)return;
    if(!done.load()){
        if(job==Job::OpenEpub||job==Job::Chapter){if(progress_label&&last_progress.empty()){last_progress="只读取当前章节，无需解压整本书";lv_label_set_text(progress_label,last_progress.c_str());}return;}
        auto now=screen::tick();if(now-progress_at<150)return;progress_at=now;
        auto received=progress.bytes.load(),total=progress.total.load();
        auto caption=received?size_text(received)+(total?" / "+size_text(total):""):"正在连接服务器…";
        if(job==Job::Download&&received){
            uint64_t elapsed=std::max<uint32_t>(1,now-job_started_at),speed=uint64_t(received)*1000/elapsed;
            if(elapsed>=500&&speed){caption+="  ·  "+size_text(speed)+"/s";if(total>received)caption+="  ·  约 "+std::to_string((total-received+speed-1)/speed)+" 秒";}
            if(progress.saving.load())caption="正在校验并保存图书…";
        }
        if(progress_label&&caption!=last_progress){lv_label_set_text(progress_label,caption.c_str());last_progress=caption;}
        if(progress_bar&&total)lv_bar_set_value(progress_bar,int(std::min<uint64_t>(100,uint64_t(received)*100/total)),LV_ANIM_OFF);
        return;
    }
    worker.join();auto finished=job;job=Job::None;
    // A completed atomic download wins a very late cancellation and stays visible.
    if(cancelled.load()&&result_file.empty()){result_epub.reset();render_return();note("已取消");return;}
    if(!result_error.empty()){result_epub.reset();error(result_error,return_view);return;}
    if(finished==Job::OpenEpub||finished==Job::Chapter){
        auto loaded=screen::tick();if(finished==Job::OpenEpub)epub=std::move(result_epub);
        chapter_index=result_chapter;reader_text=std::move(result_text);pages.clear();page_origin=reading_end=0;append_page();page=0;show_page();
        fprintf(stderr,"[crosspoint] EPUB chapter=%zu/%zu text=%zu load_ms=%u layout_ms=%u\n",chapter_index+1,epub->chapter_count(),reader_text.size(),loaded-job_started_at,screen::tick()-loaded);return;
    }
    if(finished==Job::Download){scan();auto found=std::find(books.begin(),books.end(),filename(result_file));if(found!=books.end())book_index=found-books.begin();library();note("下载完成 · 回车阅读："+filename(result_file));return;}
    int restore=0;
    if(pending_navigation==Navigation::Root)history.clear();
    else if(pending_navigation==Navigation::Forward&&!feed.url.empty()){
        if(history.size()>=32)history.erase(history.begin());history.push_back({feed.url,entry_index});
    }else if(pending_navigation==Navigation::Back&&!history.empty()){restore=history.back().index;history.pop_back();}
    else if(pending_navigation==Navigation::Refresh)restore=entry_index;
    feed=std::move(result_feed);entry_index=restore;catalog();
}
void submit_search(){
    search_query=lv_textarea_get_text(query_field);try{begin_feed(crosspoint::search_url(feed.search,search_query),Navigation::Forward);}catch(const std::exception& e){note(e.what());}
}
void search(){
    if(feed.search.empty()){note("这个目录没有提供搜索");return;}view=View::Search;frame("搜索书库");panel(16,48,768,245);
    label("输入书名、作者或关键词",34,77,728);query_field=lv_textarea_create(lv_screen_active());lv_obj_set_pos(query_field,34,119);lv_obj_set_size(query_field,732,48);
    lv_textarea_set_one_line(query_field,true);lv_textarea_set_max_length(query_field,160);lv_textarea_set_text(query_field,search_query.c_str());lv_obj_set_style_text_color(query_field,lv_color_hex(ink),0);
    if(font)lv_obj_set_style_text_font(query_field,font,0);lv_obj_add_state(query_field,LV_STATE_FOCUSED);lv_textarea_set_cursor_pos(query_field,LV_TEXTAREA_CURSOR_LAST);
    label("支持 Calibre 查询，例如 author: 或 formats:EPUB",34,188,724,muted);
    button("搜索",34,232,142,44,[]{submit_search();});button("返回",188,232,118,44,[]{catalog();});footer("实体键盘输入  ·  回车搜索  ·  右上退格删除  ·  返回取消");
}
void save_settings(){
    Server s{lv_textarea_get_text(fields[0]),trim(lv_textarea_get_text(fields[1])),lv_textarea_get_text(fields[2]),lv_textarea_get_text(fields[3])};
    try{crosspoint::url_origin(s.url);auto old=servers;if(servers.empty())servers.push_back(s);else servers[0]=s;
        try{save_servers();}catch(...){servers=std::move(old);throw;}
        unlink(draft_path().c_str());have_settings_draft=false;feed={};history.clear();library();note("服务器已保存 · O 打开在线书库");
    }catch(const std::exception& e){save_draft();note(e.what());}
}
void settings(){
    view=View::Settings;frame("OPDS 服务器设置");panel(15,45,770,250);
    const char* names[]={"书库名称","OPDS 地址","用户名（可留空）","密码（可留空）"};
    Server initial=have_settings_draft?settings_draft:servers.empty()?Server{}:servers[0];
    std::string values[]={initial.name,initial.url,initial.user,initial.password};
    for(int i=0;i<4;++i){label(names[i],28,64+i*53,192,muted);fields[i]=lv_textarea_create(lv_screen_active());lv_obj_set_pos(fields[i],226,55+i*53);lv_obj_set_size(fields[i],532,44);
        lv_textarea_set_one_line(fields[i],true);lv_textarea_set_password_mode(fields[i],i==3);lv_textarea_set_max_length(fields[i],i==1?2048:256);lv_textarea_set_text(fields[i],values[i].c_str());
        lv_obj_set_style_bg_color(fields[i],lv_color_hex(0xfffbf1),0);lv_obj_set_style_text_color(fields[i],lv_color_hex(ink),0);if(font)lv_obj_set_style_text_font(fields[i],font,0);
        for(auto event:{LV_EVENT_PRESSED,LV_EVENT_CLICKED,LV_EVENT_FOCUSED})lv_obj_add_event_cb(fields[i],[](lv_event_t* e){focus_field(int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e))));},event,reinterpret_cast<void*>(intptr_t(i)));
    }focus_field(field);footer("↑↓ 切换字段  ·  回车下一项 / 保存  ·  返回取消  ·  无需登录时留空");
}
void key(uint32_t k){
    if(k==screen::KEY_HOME){cancelled=true;screen::quit=true;return;}
    if(k==screen::KEY_FONT_UP||k==screen::KEY_FONT_DOWN){if(view==View::Reader)resize_reader(k==screen::KEY_FONT_UP?2:-2);return;}
    if(view==View::Busy){if(k==screen::KEY_EXIT){cancelled=true;note("正在取消…");}return;}
    if(k==screen::KEY_EXIT){
        if(view==View::Error)render_return();
        else if(view==View::Book||view==View::Search)catalog();
        else if(view==View::Catalog&&!history.empty())begin_feed(history.back().url,Navigation::Back);
        else if(view==View::Library)screen::quit=true;else library();return;
    }
    if(k==screen::KEY_MODE){if(view==View::Settings||view==View::Search)note(screen::caps_lock()?"ABC · 大写":"abc · 小写");return;}
    if(view==View::Settings){
        if(k==LV_KEY_UP)focus_field(field-1,true);else if(k==LV_KEY_DOWN)focus_field(field+1,true);
        else if(k==LV_KEY_LEFT)lv_textarea_cursor_left(fields[field]);else if(k==LV_KEY_RIGHT)lv_textarea_cursor_right(fields[field]);
        else if(k==LV_KEY_ENTER){if(field<3){focus_field(field+1,true);save_draft();}else save_settings();}
        else if(k==LV_KEY_BACKSPACE){lv_textarea_delete_char(fields[field]);save_draft();}
        else if(k>=32&&k<127){lv_textarea_add_char(fields[field],k);save_draft();}return;
    }
    if(view==View::Search){
        if(k==LV_KEY_ENTER)submit_search();else if(k==LV_KEY_BACKSPACE)lv_textarea_delete_char(query_field);
        else if(k==LV_KEY_LEFT)lv_textarea_cursor_left(query_field);else if(k==LV_KEY_RIGHT)lv_textarea_cursor_right(query_field);
        else if(k>=32&&k<127)lv_textarea_add_char(query_field,k);return;
    }
    bool up=k==LV_KEY_UP||k=='w'||k=='W',down=k==LV_KEY_DOWN||k=='s'||k=='S';
    bool left=k==LV_KEY_LEFT||k=='a'||k=='A',right=k==LV_KEY_RIGHT||k=='d'||k=='D';
    if(view==View::Reader){if(epub&&(up||down)){load_chapter(down?1:-1);return;}
    if(left&&page==0&&page_origin){auto previous=crosspoint::previous_page(reader_text,page_origin,page_fits);page_origin-=previous.size();pages.insert(pages.begin(),std::move(previous));}
    else if(left&&page>0)--page;else if(right||k==LV_KEY_ENTER){
        if(page+1==int(pages.size())&&reading_end<reader_text.size())append_page();
        if(page+1<int(pages.size()))++page;else if(epub&&reading_end>=reader_text.size()){load_chapter(1);return;}
    }show_page();return;}
    if(view==View::Catalog){
        if((k=='f'||k=='F')){search();return;}if(k=='o'||k=='O'){begin_feed(feed.url,Navigation::Refresh);return;}
        if(left||right){change_catalog_page(right?1:-1);return;}
        if(up&&entry_index>0)--entry_index;else if(down&&entry_index+1<int(feed.items.size()))++entry_index;
        else if(k==LV_KEY_ENTER){open_entry();return;}catalog();return;
    }
    if(view==View::Book){auto& formats=feed.items[entry_index].formats;
        if(up&&format_index>0)--format_index;else if(down&&format_index+1<int(formats.size()))++format_index;
        else if(k==LV_KEY_ENTER){download();return;}details();return;
    }
    if(view==View::Library){
        if(k=='s'||k=='S'){field=0;settings();return;}
        if(k=='o'||k=='O'){if(servers.empty())settings();else if(feed.url.empty())begin_feed(servers[0].url,Navigation::Root);else catalog();return;}
        if(k=='r'||k=='R'){scan();library();return;}
        if(up&&book_index>0)--book_index;else if((k==LV_KEY_DOWN||k=='x'||k=='X')&&book_index+1<int(books.size()))++book_index;
        else if(left)book_index=std::max(0,book_index-rows);else if(right)book_index=std::min(std::max(0,int(books.size())-1),book_index+rows);
        else if(k==LV_KEY_ENTER){open_book();return;}library();
    }
}
}
int main(){
    signal(SIGINT,signal_stop);signal(SIGTERM,signal_stop);mkdir(data_dir().c_str(),0700);mkdir(book_dir().c_str(),0700);
    if(!screen::open())return 1;
    FontShortcuts font_shortcuts;
    try{reader_size=std::clamp(Json::parse(c1::read_file(data_dir()+"/reader.json",4096)).value("font_size",18),14,32);}catch(...){}
    auto font_path=c1::root()+"/shared/NotoSansSC-Regular.ttf";
    int font_fd=open(font_path.c_str(),O_RDONLY|O_CLOEXEC);struct stat font_stat{};
    if(font_fd>=0){if(fstat(font_fd,&font_stat)==0&&font_stat.st_size>0&&font_stat.st_size<=16*1024*1024){
        font_map_size=font_stat.st_size;font_map=mmap(nullptr,font_map_size,PROT_READ,MAP_PRIVATE,font_fd,0);
        if(font_map!=MAP_FAILED)font=lv_tiny_ttf_create_data_ex(font_map,font_map_size,18,LV_FONT_KERNING_NORMAL,1024);
    }close(font_fd);}
    if(!font)font=lv_tiny_ttf_create_file(("A:"+font_path).c_str(),18);
    reader_font=font_map!=MAP_FAILED?lv_tiny_ttf_create_data_ex(font_map,font_map_size,reader_size,LV_FONT_KERNING_NORMAL,1024):lv_tiny_ttf_create_file(("A:"+font_path).c_str(),reader_size);
    load_servers();load_draft();scan();library();
    while(!stopped&&!screen::quit){lv_timer_handler();
        // Apply the touch event just dispatched by LVGL before later key input,
        // so tapping a page/field and immediately typing cannot reopen the old view.
        if(deferred&&!stopped&&!screen::quit){auto action=std::move(deferred);deferred={};action();}
        for(uint32_t k;(k=screen::take_key());)key(k);poll_job();usleep(8000);
    }
    cancelled=true;if(worker.joinable())worker.join();
    lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);
    if(reader_font)lv_tiny_ttf_destroy(reader_font);if(font)lv_tiny_ttf_destroy(font);if(font_map!=MAP_FAILED)munmap(font_map,font_map_size);screen::close();return 0;
}
