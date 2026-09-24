#include "display.hpp"
#include "net.hpp"
#include <lvgl.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <future>
#include <netdb.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <cstdint>
#include "lv_tiny_ttf.h"
#include <tinyalsa/mixer.h>

namespace {
struct Station { std::string name, country_code, url, uuid; int bitrate=0; };
struct Category { std::string name,key; int count=0; };
enum class Browse { Popular, Country, Genre, Mood, Group, Saved, Search };
struct DefaultRadioStation {const char *name;const char *url;};
constexpr DefaultRadioStation kDefaultRadioStations[]={
    {"北京音乐广播","http://lhttp.qingting.fm/live/332/64k.mp3"},
    {"北京文艺广播","http://lhttp.qingting.fm/live/333/64k.mp3"},
    {"北京交通广播","http://lhttp.qingting.fm/live/336/64k.mp3"},
    {"北京新闻广播","http://lhttp.qingting.fm/live/339/64k.mp3"},
    {"天津滨海音乐","http://lhttp.qingting.fm/live/20003/64k.mp3"},
    {"FM100.8","http://lhttp.qingting.fm/live/20212227/64k.mp3"},
    {"河北新闻FM104.3","http://lhttp.qingting.fm/live/1644/64k.mp3"},
    {"河北音乐","http://lhttp.qingting.fm/live/1649/64k.mp3"},
    {"河北交通FM99.2","http://lhttp.qingting.fm/live/1646/64k.mp3"},
    {"FM90.5","http://lhttp.qingting.fm/live/20212269/64k.mp3"},
    {"怀旧金曲964","http://lhttp.qingting.fm/live/5021555/64k.mp3"},
    {"河北文艺90.7","http://lhttp.qingting.fm/live/4868/64k.mp3"},
    {"年代96.5","http://lhttp.qingting.fm/live/5022038/64k.mp3"},
    {"Radio Suara Kupang FM","https://ssg.streamingmurah.com:8042/;;"}
};
std::vector<Station> stations;
Station playing_station;
std::vector<Category> categories;
std::vector<Station> favorites;
std::vector<std::string> legacy_favorites;
lv_obj_t *status_line=nullptr,*query=nullptr,*badge=nullptr,*badge_text=nullptr;
lv_obj_t *now_name=nullptr,*now_meta=nullptr,*volume_text=nullptr,*volume_bar=nullptr;
lv_font_t *font_small=nullptr,*font=nullptr,*font_large=nullptr;
struct mixer *audio_mixer=nullptr;
struct mixer_ctl *volume_ctl=nullptr;
int selected=0,playing_index=-1,player_in=-1,player_pid=-1,last_volume=-2;
uint32_t last_volume_poll=0,loading_started=0,connecting_started=0;
bool query_mode=false,add_mode=false,busy=false,category_list=false,connecting=false,content_loaded=false;
Browse browse=Browse::Popular;
int add_step=0;
std::string current_query,selected_category,working_message,add_name,add_url;
std::future<Json> request_job;
std::function<void(const Json&)> request_done;
volatile sig_atomic_t stopped=0;
void signal_stop(int){stopped=1;}
constexpr uint32_t ink=0x203732,paper=0xf7f2e6,teal=0x34736b,brass=0xa77d3d,muted=0x687b73;

lv_obj_t *label(const char *text,int x,int y,int width,uint32_t color=ink,int size=18){
    auto *o=lv_label_create(lv_screen_active());lv_label_set_text(o,text);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,width);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_font_t *f=size<=16&&font_small?font_small:size>=22&&font_large?font_large:font;
    if(f)lv_obj_set_style_text_font(o,f,0);return o;
}
lv_obj_t *panel(int x,int y,int w,int h,uint32_t color,int radius=12){
    auto *o=lv_obj_create(lv_screen_active());lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_radius(o,radius,0);lv_obj_set_style_border_width(o,1,0);lv_obj_set_style_border_color(o,lv_color_hex(0xd5cdbb),0);
    lv_obj_set_style_shadow_width(o,0,0);return o;
}
void update_badge(){
    if(!badge_text)return;
    const char *text=busy?"LOADING":connecting?"CONNECTING":player_pid>0?"ON AIR":"READY";
    lv_label_set_text(badge_text,text);
    lv_obj_set_style_bg_color(badge,lv_color_hex(busy||connecting?brass:player_pid>0?teal:0x5a6d65),LV_PART_MAIN);
}
void status(const std::string &s){if(status_line)lv_label_set_text(status_line,s.c_str());update_badge();}
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");if(a==std::string::npos)return {};auto b=s.find_last_not_of(" \t\r\n");return s.substr(a,b-a+1);}
std::string query_text(){return query?trim(lv_textarea_get_text(query)):current_query;}
std::string path(const char *leaf){return c1::data()+"/airtune/"+leaf;}
const char *category_source_name(Browse source){return source==Browse::Country?"country":"genre";}
std::string category_cache_path(Browse source){return path(source==Browse::Country?"categories-country.json":"categories-genre.json");}

int system_volume(){
    if(!audio_mixer)audio_mixer=mixer_open(0);
    if(!volume_ctl&&audio_mixer)volume_ctl=mixer_get_ctl_by_name(audio_mixer,"softvolume");
    long values[2];if(!volume_ctl||mixer_ctl_get_array(volume_ctl,values,2))return -1;
    int low=mixer_ctl_get_range_min(volume_ctl),high=mixer_ctl_get_range_max(volume_ctl);if(high<=low)return -1;
    return std::clamp<int>(int(((values[0]+values[1])/2-low)*100/(high-low)),0,100);
}
void update_player_panel(){
    int index=selected;
    const Station *on_air=player_pid>0&&!playing_station.name.empty()?&playing_station:(index>=0&&index<int(stations.size())?&stations[index]:nullptr);
    if(now_name){
        const std::string name=on_air?on_air->name:"Choose a station";
        lv_label_set_text(now_name,name.c_str());
    }
    if(now_meta){
        std::string meta=on_air?on_air->country_code:"INTERNET RADIO";
        if(meta.empty())meta="WORLD";
        if(on_air&&on_air->bitrate)meta+="  ·  "+std::to_string(on_air->bitrate)+" kbps";
        lv_label_set_text(now_meta,meta.c_str());
    }
}
void update_volume(bool force=false){
    int value=system_volume();if(!force&&value==last_volume)return;last_volume=value;
    if(volume_text){std::string shown=add_mode?(value<0?"--":value==0?"MUTED":std::to_string(value)+"%"):(value<0?"SYSTEM VOLUME  --":value==0?"SYSTEM VOLUME  MUTED":"SYSTEM VOLUME  "+std::to_string(value)+"%");lv_label_set_text(volume_text,shown.c_str());}
    if(volume_bar&&value>=0)lv_bar_set_value(volume_bar,value,LV_ANIM_OFF);
}
bool valid_stream_url(const std::string &url){return url.size()<=2048&&(url.rfind("http://",0)==0||url.rfind("https://",0)==0)&&url.find_first_of("\r\n\t ")==std::string::npos;}
bool same_station(const Station &a,const Station &b){return (!a.uuid.empty()&&!b.uuid.empty()&&a.uuid==b.uuid)||(!a.url.empty()&&a.url==b.url);}
std::vector<Station> default_stations(){std::vector<Station> result;for(const auto &item:kDefaultRadioStations){Station s;s.name=item.name;s.url=item.url;result.push_back(std::move(s));}return result;}
Json station_json(const Station &s){return {{"name",s.name},{"country_code",s.country_code},{"url",s.url},{"uuid",s.uuid},{"bitrate",s.bitrate}};}
bool parse_saved_station(const Json &item,Station &s){
    if(!item.is_object()||!item.contains("name")||!item["name"].is_string()||!item.contains("url")||!item["url"].is_string())return false;
    s.name=trim(item["name"].get<std::string>());s.url=trim(item["url"].get<std::string>());
    if(s.name.empty()||!valid_stream_url(s.url))return false;
    if(item.contains("country_code")&&item["country_code"].is_string())s.country_code=item["country_code"].get<std::string>();
    if(item.contains("uuid")&&item["uuid"].is_string())s.uuid=item["uuid"].get<std::string>();
    if(item.contains("bitrate")&&item["bitrate"].is_number_integer())s.bitrate=item["bitrate"].get<int>();
    return true;
}
void favorites_save(){
    try{Json data={{"version",1},{"stations",Json::array()},{"legacy_ids",legacy_favorites}};
        for(const auto &s:favorites)data["stations"].push_back(station_json(s));
        c1::save_private(path("saved-stations.json"),data.dump(2)+"\n");
    }catch(...){}
}
void favorites_load(){
    favorites.clear();legacy_favorites.clear();bool has_new_file=false;std::string raw;
    try{raw=c1::read_file(path("saved-stations.json"),262144);has_new_file=true;}catch(...){}
    if(has_new_file){
        try{auto data=Json::parse(raw);if(data.is_object()&&data.value("version",0)==1&&data.contains("stations")&&data["stations"].is_array()&&data.contains("legacy_ids")&&data["legacy_ids"].is_array()){
            for(const auto &item:data["stations"]){Station s;if(parse_saved_station(item,s))favorites.push_back(std::move(s));}
            for(const auto &item:data["legacy_ids"])if(item.is_string()&&!item.get<std::string>().empty())legacy_favorites.push_back(item.get<std::string>());
            return;
        }}catch(...){}
        try{c1::save_private(path("saved-stations.recovery.json"),raw);}catch(...){}
    }
    favorites=default_stations();
    try{auto old=Json::parse(c1::read_file(path("favorites.json"),32768));if(old.is_array())for(const auto &item:old)if(item.is_string()&&!item.get<std::string>().empty())legacy_favorites.push_back(item.get<std::string>());}catch(...){}
    favorites_save();
}
bool is_favorite(const Station &s){return std::find_if(favorites.begin(),favorites.end(),[&](const Station &saved){return same_station(saved,s);})!=favorites.end();}
void paint();
std::vector<Station> saved_stations_view(){
    std::vector<Station> view=favorites;
    for(const auto &id:legacy_favorites){
        const bool represented=std::any_of(view.begin(),view.end(),[&](const Station &s){return s.uuid==id;});
        if(!represented){Station old;old.uuid=id;old.name="Older save · "+id.substr(0,std::min<size_t>(8,id.size()));view.push_back(std::move(old));}
    }
    return view;
}
bool is_unresolved_save(const Station &s){return s.url.empty()&&!s.uuid.empty()&&std::find(legacy_favorites.begin(),legacy_favorites.end(),s.uuid)!=legacy_favorites.end();}
void begin_add_station(){add_mode=true;query_mode=false;add_step=0;add_name.clear();add_url.clear();paint();status("Enter a station name · Enter continues");}
void play();

std::vector<std::string> radio_api_hosts(){
    constexpr const char *fallback="de1.api.radio-browser.info";
    constexpr const char *suffix=".api.radio-browser.info";
    std::vector<std::string> hosts;addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;addrinfo *addresses=nullptr;
    if(getaddrinfo("all.api.radio-browser.info",nullptr,&hints,&addresses)==0){
        for(auto *address=addresses;address;address=address->ai_next){char name[NI_MAXHOST];
            if(getnameinfo(address->ai_addr,address->ai_addrlen,name,sizeof(name),nullptr,0,NI_NAMEREQD)!=0)continue;
            std::string host=name;size_t n=std::strlen(suffix);
            if(host.size()<=n||host.compare(host.size()-n,n,suffix)!=0)continue;
            if(std::find(hosts.begin(),hosts.end(),host)==hosts.end())hosts.push_back(std::move(host));
        }
        freeaddrinfo(addresses);
    }
    if(std::find(hosts.begin(),hosts.end(),fallback)==hosts.end())hosts.push_back(fallback);
    if(hosts.size()>1)std::rotate(hosts.begin(),hosts.begin()+(getpid()%hosts.size()),hosts.end());return hosts;
}
Json radio_json(const std::string &endpoint){
    std::string error="Radio directory unavailable";
    for(const auto &host:radio_api_hosts())try{
        c1::reset_requests(7000);
        auto r=c1::http("GET","https://"+host+endpoint,{"User-Agent: C1Max-Airtune/1.1","Accept: application/json"});
        if(r.status!=200)throw std::runtime_error("Radio directory HTTP "+std::to_string(r.status));
        return Json::parse(r.body);
    }catch(const std::exception &e){error=e.what();}
    throw std::runtime_error(error);
}
std::vector<Station> parse_stations(const Json &data){
    std::vector<Station> fresh;if(!data.is_array())return fresh;
    for(const auto &v:data){Station s;
        if(v.contains("name")&&v["name"].is_string())s.name=v["name"].get<std::string>();
        if(v.contains("countrycode")&&v["countrycode"].is_string())s.country_code=v["countrycode"].get<std::string>();
        if(v.contains("url_resolved")&&v["url_resolved"].is_string())s.url=v["url_resolved"].get<std::string>();
        if(v.contains("stationuuid")&&v["stationuuid"].is_string())s.uuid=v["stationuuid"].get<std::string>();
        if(v.contains("bitrate")&&v["bitrate"].is_number_integer())s.bitrate=v["bitrate"].get<int>();
        if(!s.name.empty()&&!s.url.empty()&&(s.url.rfind("http://",0)==0||s.url.rfind("https://",0)==0))fresh.push_back(std::move(s));
    }return fresh;
}
void work(const std::string &message,std::function<Json()> run,std::function<void(const Json&)> done){
    if(busy)return;busy=true;working_message=message;loading_started=screen::tick();request_done=std::move(done);status(message);
    request_job=std::async(std::launch::async,[run=std::move(run)]{try{return Json{{"ok",true},{"result",run()}};}catch(const std::exception &e){return Json{{"ok",false},{"error",e.what()}};}catch(...){return Json{{"ok",false},{"error","Unknown network error"}};}});
}
Json station_search(const std::string &query_value){
    std::string endpoint="/json/stations/search?limit=16&hidebroken=true&order=clickcount&reverse=true";
    if(!query_value.empty())endpoint+="&name="+c1::encode(query_value);
    return radio_json(endpoint);
}
std::string browse_name(Browse b){switch(b){case Browse::Popular:return "POPULAR";case Browse::Country:return "COUNTRY";case Browse::Genre:return "GENRE";case Browse::Mood:return "MOOD";case Browse::Group:return "GROUP";case Browse::Saved:return "SAVED";case Browse::Search:return "SEARCH";}return "RADIO";}
std::vector<Category> moods(){return {{"Chill","chill",0},{"Ambient","ambient",0},{"Lounge","lounge",0},{"Relax","relax",0},{"Sleep","sleep",0},{"Focus","focus",0},{"Meditation","meditation",0},{"Easy Listening","easy listening",0},{"Instrumental","instrumental",0},{"Lo-Fi","lofi",0},{"Deep House","deep house",0},{"Nature","nature",0}};}
std::vector<Category> groups(){return {{"RauteMusik","RauteMusik",0},{"SomaFM","SomaFM",0},{"RADIO BOB!","RADIO BOB",0},{"FluxFM","FluxFM",0},{"181.FM","181.FM",0},{"RPR1.","RPR1",0},{"Qingting FM","Qingting",0},{"Cadena SER","Cadena SER",0},{"bigFM","bigFM",0},{"0nlineradio","0nlineradio",0},{"BBC","BBC",0},{"sunshine live","sunshine live",0},{"Allzic Radio","Allzic",0},{"France Bleu","France Bleu",0},{"WDR","WDR",0},{"Jazz Radio","Jazz Radio",0},{"Radio Paradise","Radio Paradise",0},{"CBC","CBC",0},{"NTS Radio","NTS",0},{"Radio France","Radio France",0}};}
struct CategoryCache {std::vector<Category> items;std::time_t fetched_at=0;bool valid=false;};
std::vector<Category> parse_categories(Browse source,const Json &result){
    std::vector<Category> parsed;if(!result.is_array())return parsed;
    for(const auto &v:result){Category c;
        if(v.contains("name")&&v["name"].is_string())c.name=v["name"].get<std::string>();
        if(source==Browse::Country&&v.contains("iso_3166_1")&&v["iso_3166_1"].is_string())c.key=v["iso_3166_1"].get<std::string>();else c.key=c.name;
        if(v.contains("stationcount")){try{c.count=v["stationcount"].is_string()?std::stoi(v["stationcount"].get<std::string>()):v["stationcount"].get<int>();}catch(...) {}}
        if(!c.name.empty()&&!c.key.empty())parsed.push_back(std::move(c));
    }
    if(source==Browse::Genre){static const std::vector<std::string> moods_to_skip={"ambient","chill","relax","lounge","sleep","focus","meditation","easy listening","instrumental","lofi","nature","mood"};
        parsed.erase(std::remove_if(parsed.begin(),parsed.end(),[&](const Category &c){std::string n=c.name;std::transform(n.begin(),n.end(),n.begin(),[](unsigned char x){return char(std::tolower(x));});return std::find(moods_to_skip.begin(),moods_to_skip.end(),n)!=moods_to_skip.end();}),parsed.end());
    }
    return parsed;
}
CategoryCache read_category_cache(Browse source){
    CategoryCache cache;
    try{auto data=Json::parse(c1::read_file(category_cache_path(source),262144));
        if(!data.is_object()||data.value("version",0)!=1||data.value("source",std::string())!=category_source_name(source)||!data.contains("fetched_at")||!data["fetched_at"].is_number_integer()||!data.contains("items")||!data["items"].is_array()||data["items"].size()>512)return cache;
        cache.fetched_at=data["fetched_at"].get<std::time_t>();
        for(const auto &v:data["items"]){if(!v.is_object()||!v.contains("name")||!v["name"].is_string()||!v.contains("key")||!v["key"].is_string())return CategoryCache{};
            Category c;c.name=v["name"].get<std::string>();c.key=v["key"].get<std::string>();if(v.contains("count")&&v["count"].is_number_integer())c.count=v["count"].get<int>();
            if(c.name.empty()||c.key.empty())return CategoryCache{};cache.items.push_back(std::move(c));
        }
        cache.valid=true;
    }catch(...){}return cache;
}
void save_category_cache(Browse source,const std::vector<Category> &items){
    try{Json data={{"version",1},{"source",category_source_name(source)},{"fetched_at",static_cast<long long>(std::time(nullptr))},{"items",Json::array()}};
        for(const auto &c:items)data["items"].push_back({{"name",c.name},{"key",c.key},{"count",c.count}});
        c1::save_private(category_cache_path(source),data.dump(2)+"\n");
    }catch(...){}
}
bool same_categories(const std::vector<Category> &a,const std::vector<Category> &b){
    if(a.size()!=b.size())return false;for(size_t i=0;i<a.size();++i)if(a[i].name!=b[i].name||a[i].key!=b[i].key||a[i].count!=b[i].count)return false;return true;
}
bool category_cache_fresh(const CategoryCache &cache){auto now=std::time(nullptr);return cache.valid&&cache.fetched_at>0&&now>=cache.fetched_at&&(now-cache.fetched_at)<6*60*60;}
void load_browse(bool force_refresh=false);
void load_category(){
    if(selected<0||selected>=int(categories.size()))return;selected_category=categories[selected].key;category_list=false;content_loaded=false;selected=0;stations.clear();paint();
    Browse source=browse;std::string key=selected_category;
    work("Loading "+browse_name(source)+" stations…",[source,key]{
        std::string endpoint;
        if(source==Browse::Country)endpoint="/json/stations/bycountrycodeexact/"+c1::encode(key)+"?limit=16&hidebroken=true&order=clickcount&reverse=true";
        else if(source==Browse::Group)endpoint="/json/stations/search?limit=16&hidebroken=true&order=clickcount&reverse=true&name="+c1::encode(key);
        else endpoint="/json/stations/bytagexact/"+c1::encode(key)+"?limit=16&hidebroken=true&order=clickcount&reverse=true";
        return radio_json(endpoint);
    },[](const Json &result){stations=parse_stations(result);selected=0;content_loaded=true;paint();status(stations.empty()?"No stations in this category":"Choose a station · Enter or tap to play");});
}
void load_browse(bool force_refresh){
    if(busy)return;selected=0;selected_category.clear();stations.clear();categories.clear();content_loaded=false;
    if(browse==Browse::Country||browse==Browse::Genre||browse==Browse::Mood||browse==Browse::Group){
        category_list=true;paint();
        if(browse==Browse::Mood||browse==Browse::Group){categories=browse==Browse::Mood?moods():groups();content_loaded=true;paint();status("Choose a "+browse_name(browse)+" category · Enter to open");return;}
        Browse source=browse;CategoryCache cache=read_category_cache(source);categories=cache.items;content_loaded=cache.valid;paint();
        if(category_cache_fresh(cache)&&!force_refresh){status("Cached "+browse_name(source)+" categories · R refreshes");return;}
        const std::string message=cache.valid?(source==Browse::Country?"Refreshing countries…":"Refreshing genres…"):(source==Browse::Country?"Loading countries…":"Loading genres…");
        work(message,[source]{return radio_json(source==Browse::Country?"/json/countries?order=stationcount&reverse=true&limit=100":"/json/tags?order=stationcount&reverse=true&limit=100");},[source](const Json &result){
            auto fresh=parse_categories(source,result);const bool changed=!same_categories(categories,fresh);categories=std::move(fresh);selected=0;content_loaded=true;save_category_cache(source,categories);
            if(changed)paint();
            status(categories.empty()?"No categories available":changed?"Category list updated · choose a "+browse_name(source)+" category":"Categories up to date · choose a "+browse_name(source)+" category");
        });return;
    }
    category_list=false;paint();
    if(browse==Browse::Popular)work("Loading popular stations…",[]{return station_search("");},[](const Json &result){stations=parse_stations(result);selected=0;content_loaded=true;paint();status(stations.empty()?"No stations available":"Popular internet stations · Enter to play");});
    else if(browse==Browse::Search){std::string q=current_query;work("Searching stations…",[q]{return station_search(q);},[](const Json &result){stations=parse_stations(result);selected=0;content_loaded=true;paint();status(stations.empty()?"No stations found · press Q to search again":"Search results · Enter to play");});}
    else if(browse==Browse::Saved){stations=saved_stations_view();selected=0;content_loaded=true;paint();
        if(legacy_favorites.empty()){status("Saved locally · A adds · Backspace removes");return;}
        std::string ids;for(const auto &id:legacy_favorites){if(!ids.empty())ids+=",";ids+=id;}
        work("Importing older saved stations…",[ids]{return radio_json("/json/stations/byuuid?uuids="+c1::encode(ids));},[](const Json &result){
            auto imported=parse_stations(result);for(const auto &s:imported)if(!is_favorite(s))favorites.push_back(s);
            std::vector<std::string> unresolved;for(const auto &id:legacy_favorites){bool found=std::any_of(imported.begin(),imported.end(),[&](const Station &s){return s.uuid==id;});if(!found)unresolved.push_back(id);}
            legacy_favorites=std::move(unresolved);favorites_save();stations=saved_stations_view();selected=0;content_loaded=true;paint();
            status(legacy_favorites.empty()?"Saved list imported · stored locally":"Some older saves need Radio-Browser to reconnect");
        });}
}

void change_browse(Browse b){if(busy)return;browse=b;query_mode=false;load_browse();}
void button_style(lv_obj_t *o,uint32_t color){lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_text_color(o,lv_color_hex(ink),0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,7,0);}
void paint(){
    std::string old_query=query?std::string(lv_textarea_get_text(query)):current_query;
    query=nullptr;now_name=now_meta=volume_text=volume_bar=nullptr;
    auto *root=lv_screen_active();lv_obj_clean(root);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(0xe9e2d2),0);lv_obj_set_style_text_color(root,lv_color_hex(ink),0);if(font)lv_obj_set_style_text_font(root,font,0);
    panel(12,6,776,38,0xf7f2e6,11);
    label("AIR / TUNE",23,10,172,ink,24);label("WORLD RADIO    /    LISTEN YOUR WAY",205,15,430,muted,16);
    badge=lv_button_create(root);lv_obj_remove_flag(badge,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_pos(badge,655,11);lv_obj_set_size(badge,120,27);button_style(badge,player_pid>0?teal:0x5a6d65);
    badge_text=lv_label_create(badge);lv_label_set_text(badge_text,"READY");lv_obj_set_width(badge_text,110);lv_label_set_long_mode(badge_text,LV_LABEL_LONG_CLIP);lv_obj_center(badge_text);lv_obj_set_style_text_align(badge_text,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(badge_text,lv_color_hex(0xffffff),0);if(font_small)lv_obj_set_style_text_font(badge_text,font_small,0);
    panel(12,50,512,232,paper,13);panel(536,50,252,232,ink,13);

    static const Browse tabs[]={Browse::Popular,Browse::Country,Browse::Genre,Browse::Mood,Browse::Group,Browse::Saved};
    static const char *tab_names[]={"TOP","COUNTRY","GENRE","MOOD","GROUP","SAVED"};
    const bool saved_page=browse==Browse::Saved&&!category_list&&!query_mode&&!add_mode;
    if(!add_mode)for(int i=0;i<6;i++){
        auto *b=lv_button_create(root);lv_obj_set_pos(b,21+i*82,56);lv_obj_set_size(b,78,31);button_style(b,browse==tabs[i]?0xc8ded3:0xeae4d6);
        auto *t=lv_label_create(b);lv_label_set_text(t,tab_names[i]);lv_obj_center(t);if(font_small)lv_obj_set_style_text_font(t,font_small,0);
        lv_obj_add_event_cb(b,[](lv_event_t *e){if(busy)return;auto i=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));static const Browse values[]={Browse::Popular,Browse::Country,Browse::Genre,Browse::Mood,Browse::Group,Browse::Saved};change_browse(values[i]);},LV_EVENT_CLICKED,reinterpret_cast<void*>(intptr_t(i)));
    }
    std::string section;
    if(category_list)section="CHOOSE A "+browse_name(browse)+" CATEGORY";
    else if(!selected_category.empty())section=selected_category;
    else if(browse==Browse::Search)section=current_query.empty()?"SEARCH RESULTS":"SEARCH  /  "+current_query;
    else if(saved_page)section="STORED ON THIS DEVICE  ·  "+std::to_string(favorites.size()+legacy_favorites.size())+" STATIONS";
    if(!add_mode&&!section.empty()){auto *section_label=label(section.c_str(),23,87,495,muted,14);lv_label_set_long_mode(section_label,LV_LABEL_LONG_DOT);}
    query=lv_textarea_create(root);lv_obj_set_pos(query,21,add_mode?168:103);lv_obj_set_size(query,saved_page?376:494,add_mode?39:31);lv_textarea_set_one_line(query,true);
    lv_textarea_set_placeholder_text(query,add_mode?(add_step==0?"Station name":"http:// or https:// stream URL"):"Q  Search stations by name");
    lv_textarea_set_max_length(query,add_mode?(add_step==0?80:2048):256);
    const std::string input_text=add_mode?(add_step==0?add_name:add_url):(query_mode?old_query:current_query);
    lv_textarea_set_text(query,input_text.c_str());lv_obj_set_style_bg_color(query,lv_color_hex(0xfffcf5),0);lv_obj_set_style_text_color(query,lv_color_hex(ink),0);
    lv_obj_set_style_border_color(query,lv_color_hex(query_mode||add_mode?teal:0xd4c8aa),0);lv_obj_set_style_border_width(query,query_mode||add_mode?2:1,0);lv_obj_set_style_radius(query,8,0);lv_obj_set_style_pad_left(query,12,0);lv_obj_set_style_pad_right(query,10,0);if(add_mode&&font)lv_obj_set_style_text_font(query,font,0);else if(font_small)lv_obj_set_style_text_font(query,font_small,0);
    if(saved_page){auto *add=lv_button_create(root);lv_obj_set_pos(add,406,103);lv_obj_set_size(add,109,31);button_style(add,teal);auto *text=lv_label_create(add);lv_label_set_text(text,"+  ADD STATION");lv_obj_center(text);lv_obj_set_style_text_color(text,lv_color_hex(0xffffff),0);if(font_small)lv_obj_set_style_text_font(text,font_small,0);lv_obj_add_event_cb(add,[](lv_event_t*){if(!busy)begin_add_station();},LV_EVENT_CLICKED,nullptr);}
    const int start=(selected/5)*5;const int total=int(category_list?categories.size():stations.size());const int shown=std::max(0,std::min(5,total-start));
    if(add_mode){
        auto step_chip=[&](int x,int width,const char *text,bool active){auto *chip=lv_button_create(root);lv_obj_remove_flag(chip,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_pos(chip,x,59);lv_obj_set_size(chip,width,23);button_style(chip,active?0xc8ded3:0xeae4d6);auto *t=lv_label_create(chip);lv_label_set_text(t,text);lv_obj_center(t);if(font_small)lv_obj_set_style_text_font(t,font_small,0);};
        step_chip(22,144,"01  STATION NAME",add_step==0);step_chip(174,144,"02  STREAM URL",add_step==1);
        label(add_step==0?"Save a local station":"Add the stream address",23,91,480,ink,24);
        label(add_step==0?"Choose a name that is easy to recognize":"Enter the direct HTTP(S) audio stream URL",23,122,480,muted,14);
        label(add_step==0?"STATION NAME":"STREAM URL",23,146,470,teal,13);
        if(add_step==1){auto *preview=label(("SAVING AS   "+add_name).c_str(),23,215,480,muted,14);lv_label_set_long_mode(preview,LV_LABEL_LONG_DOT);}
        auto *action=lv_button_create(root);lv_obj_remove_flag(action,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_pos(action,23,239);lv_obj_set_size(action,154,27);button_style(action,teal);
        auto *action_text=lv_label_create(action);lv_label_set_text(action_text,add_step==0?"ENTER   CONTINUE":"ENTER   SAVE STATION");lv_obj_center(action_text);lv_obj_set_style_text_color(action_text,lv_color_hex(0xffffff),0);if(font_small)lv_obj_set_style_text_font(action_text,font_small,0);
        label("BACKSPACE edits   ·   RETURN cancels",191,245,318,muted,14);
    }
    for(int i=0;!add_mode&&i<shown;i++){
        int index=start+i,y=138+i*28;uint32_t bg=index==selected?0xd8e7dc:0xf7f2e6;
        auto *row=lv_obj_create(root);lv_obj_remove_flag(row,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_pos(row,20,y);lv_obj_set_size(row,496,26);
        lv_obj_set_style_bg_color(row,lv_color_hex(bg),0);lv_obj_set_style_bg_opa(row,LV_OPA_COVER,0);lv_obj_set_style_border_width(row,index==selected?1:0,0);lv_obj_set_style_border_color(row,lv_color_hex(0xabc5b7),0);lv_obj_set_style_radius(row,6,0);
        std::string title,detail;
        if(category_list){const auto &c=categories[index];title=c.name;detail=c.count>0?std::to_string(c.count)+" stations":"OPEN";}
        else {const auto &s=stations[index];title=s.name;detail=is_unresolved_save(s)?"RECONNECT":(s.country_code.empty()?"WORLD":s.country_code)+(s.bitrate?"  ·  "+std::to_string(s.bitrate)+"k":"");}
        auto *name=label(title.c_str(),29,y+3,350,ink,16);lv_obj_set_height(name,22);lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
        auto *info=label(detail.c_str(),384,y+5,105,muted,14);lv_obj_set_height(info,18);lv_label_set_long_mode(info,LV_LABEL_LONG_CLIP);
        if(!category_list&&(is_favorite(stations[index])||is_unresolved_save(stations[index])))label("*",498,y+4,14,brass,16);
        lv_obj_add_event_cb(row,[](lv_event_t *e){if(busy)return;selected=int(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));if(category_list)load_category();else play();},LV_EVENT_CLICKED,reinterpret_cast<void*>(intptr_t(index)));
    }
    if(!add_mode&&total==0){std::string empty=content_loaded?(category_list?"No categories available":"No stations available"):(category_list?"Loading categories…":"Loading stations…");auto *l=label(empty.c_str(),31,166,450,muted,18);lv_label_set_long_mode(l,LV_LABEL_LONG_DOT);}

    if(add_mode){
        label("LOCAL SAVED LIST",553,61,218,0xd6c9ad,14);
        label("ON THIS DEVICE",553,86,218,0xf8f2e6,21);
        const std::string count=std::to_string(favorites.size()+legacy_favorites.size())+" STATIONS";
        label(count.c_str(),553,119,218,0xc5d6c8,16);
        label("Names and stream URLs stay\nin this local list.",553,148,218,0xc5d6c8,14);
        label("SYSTEM VOLUME",553,193,218,0xc5d6c8,13);
        volume_text=label("--",553,211,218,0xf5e7c9,16);
        volume_bar=lv_bar_create(root);lv_obj_set_pos(volume_bar,553,239);lv_obj_set_size(volume_bar,218,10);lv_bar_set_range(volume_bar,0,100);lv_bar_set_value(volume_bar,last_volume<0?0:last_volume,LV_ANIM_OFF);
        lv_obj_set_style_bg_color(volume_bar,lv_color_hex(0x3d5650),LV_PART_MAIN);lv_obj_set_style_bg_opa(volume_bar,LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_radius(volume_bar,4,LV_PART_MAIN);
        lv_obj_set_style_bg_color(volume_bar,lv_color_hex(0x7fc5a8),LV_PART_INDICATOR);lv_obj_set_style_bg_opa(volume_bar,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_set_style_radius(volume_bar,4,LV_PART_INDICATOR);
        label("Hardware keys  − / +",553,259,220,0xc5d6c8,13);
    }else{
        label("NOW PLAYING",553,61,218,0xd6c9ad,14);
        now_name=label("Choose a station",553,86,218,0xf8f2e6,22);lv_obj_set_height(now_name,38);lv_label_set_long_mode(now_name,LV_LABEL_LONG_DOT);
        now_meta=label("WORLD STREAMS",553,128,218,0xc5d6c8,14);lv_label_set_long_mode(now_meta,LV_LABEL_LONG_DOT);
        label("AUDIO OUTPUT",553,157,218,0xc5d6c8,13);
        volume_text=label("SYSTEM VOLUME  --",553,178,218,0xf5e7c9,16);
        volume_bar=lv_bar_create(root);lv_obj_set_pos(volume_bar,553,207);lv_obj_set_size(volume_bar,218,12);lv_bar_set_range(volume_bar,0,100);lv_bar_set_value(volume_bar,last_volume<0?0:last_volume,LV_ANIM_OFF);
        lv_obj_set_style_bg_color(volume_bar,lv_color_hex(0x3d5650),LV_PART_MAIN);lv_obj_set_style_bg_opa(volume_bar,LV_OPA_COVER,LV_PART_MAIN);lv_obj_set_style_radius(volume_bar,4,LV_PART_MAIN);
        lv_obj_set_style_bg_color(volume_bar,lv_color_hex(0x7fc5a8),LV_PART_INDICATOR);lv_obj_set_style_bg_opa(volume_bar,LV_OPA_COVER,LV_PART_INDICATOR);lv_obj_set_style_radius(volume_bar,4,LV_PART_INDICATOR);
        label("Hardware volume keys  − / +",553,232,220,0xc5d6c8,13);
    }
    status_line=label("",16,287,768,muted,16);lv_label_set_long_mode(status_line,LV_LABEL_LONG_DOT);
    const char *help=add_mode?"POWER returns to launcher":browse==Browse::Saved?"A add station   BACKSPACE / F delete   ENTER play / stop   POWER menu":"↑↓ browse   ENTER choose / play   Q search   F save / remove   SAVED: A add, BACKSPACE delete   POWER menu";
    label(help,16,312,770,muted,14);
    update_player_panel();update_volume(true);update_badge();
}

void stop_player(){
    if(player_in>=0){write(player_in,"quit\n",5);close(player_in);player_in=-1;}
    if(player_pid>0){for(int i=0;i<20;i++){if(waitpid(player_pid,nullptr,WNOHANG)==player_pid){player_pid=-1;break;}usleep(50000);}if(player_pid>0){kill(player_pid,SIGTERM);waitpid(player_pid,nullptr,0);player_pid=-1;}}
    playing_index=-1;playing_station=Station{};connecting=false;update_player_panel();update_badge();
}
void play(){
    if(stations.empty()||selected<0||selected>=int(stations.size()))return;
    if(!valid_stream_url(stations[selected].url)){status("This older save needs Radio-Browser to restore its stream URL");return;}
    stop_player();const Station station=stations[selected];int pipes[2];if(pipe(pipes)){status("Audio process unavailable");return;}
    pid_t parent=getpid(),pid=fork();
    if(pid==0){prctl(PR_SET_PDEATHSIG,SIGTERM);if(getppid()!=parent)_exit(1);dup2(pipes[0],STDIN_FILENO);int log=open("/dev/null",O_WRONLY);if(log>=0){dup2(log,STDOUT_FILENO);dup2(log,STDERR_FILENO);}close(pipes[0]);close(pipes[1]);execlp("mplayer","mplayer","-noconfig","all","-slave","-quiet","-ao","media","-cache","256","-cache-min","5",station.url.c_str(),(char*)nullptr);_exit(127);}
    close(pipes[0]);if(pid<0){close(pipes[1]);status("Cannot start MPlayer");return;}
    player_pid=pid;player_in=pipes[1];playing_station=station;playing_index=selected;connecting=true;connecting_started=screen::tick();update_player_panel();status("Connecting to "+station.name+"…");
}
void open_selected(){if(busy)return;if(category_list)load_category();else play();}
void change_selected(int delta){
    const int count=int(category_list?categories.size():stations.size());if(count<=0)return;
    selected=(selected+delta+count)%count;paint();
}
void remove_saved_selection(){
    if(browse!=Browse::Saved||category_list||selected<0||selected>=int(stations.size()))return;
    const Station target=stations[selected];
    auto found=std::find_if(favorites.begin(),favorites.end(),[&](const Station &s){return same_station(s,target);});
    if(found!=favorites.end())favorites.erase(found);
    else if(is_unresolved_save(target))legacy_favorites.erase(std::remove(legacy_favorites.begin(),legacy_favorites.end(),target.uuid),legacy_favorites.end());
    else return;
    favorites_save();stations=saved_stations_view();selected=stations.empty()?0:std::min(selected,int(stations.size())-1);content_loaded=true;paint();status("Removed from local SAVED list");
}
void key(uint32_t k){
    if(k==screen::KEY_HOME){stop_player();screen::quit=true;return;}
    if(k==screen::KEY_MODE)return;
    if(busy)return;
    if(add_mode){
        if(k==screen::KEY_EXIT){add_mode=false;add_step=0;add_name.clear();add_url.clear();paint();status("Add station cancelled");return;}
        if(k==LV_KEY_BACKSPACE){lv_textarea_delete_char(query);return;}
        if(k==LV_KEY_ENTER){
            const std::string text=query_text();
            if(add_step==0){if(text.empty()){status("Station name cannot be empty");return;}add_name=text;add_step=1;add_url.clear();paint();status("Enter an HTTP(S) stream URL");return;}
            if(!valid_stream_url(text)){status("Use a valid http:// or https:// stream URL");return;}
            Station added;added.name=add_name;added.url=text;
            if(is_favorite(added)){status("This stream is already in SAVED");return;}
            favorites.push_back(std::move(added));favorites_save();add_mode=false;add_step=0;browse=Browse::Saved;stations=saved_stations_view();selected=int(stations.size())-1;content_loaded=true;paint();status("Added to SAVED · stored on this device");return;
        }
        if(k>=32&&k<127)lv_textarea_add_char(query,k);return;
    }
    if(query_mode){if(k==screen::KEY_EXIT){query_mode=false;paint();status("Search cancelled");return;}
        if(k==LV_KEY_BACKSPACE)lv_textarea_delete_char(query);
        else if(k==LV_KEY_ENTER){current_query=query_text();query_mode=false;browse=Browse::Search;load_browse();}
        else if(k>=32&&k<127)lv_textarea_add_char(query,k);return;}
    if(k==screen::KEY_EXIT){
        if(!category_list&&(browse==Browse::Country||browse==Browse::Genre||browse==Browse::Mood||browse==Browse::Group)){category_list=true;selected=0;categories.clear();load_browse();}
        else if(browse!=Browse::Popular)change_browse(Browse::Popular);
        else if(player_pid>0)stop_player();return;
    }
    if(k=='q'||k=='Q'){query_mode=true;lv_textarea_set_text(query,"");paint();status("Type a station name · Enter searches");return;}
    if(browse==Browse::Saved&&(k=='a'||k=='A')){begin_add_station();return;}
    if(browse==Browse::Saved&&(k==LV_KEY_BACKSPACE||k=='f'||k=='F')){remove_saved_selection();return;}
    if(k=='f'||k=='F'){if(!category_list&&!stations.empty()&&selected<int(stations.size())){const Station chosen=stations[selected];auto it=std::find_if(favorites.begin(),favorites.end(),[&](const Station &saved){return same_station(saved,chosen);});bool saved=it==favorites.end();if(saved)favorites.push_back(chosen);else favorites.erase(it);favorites_save();paint();status(saved?"Station saved locally":"Station removed from SAVED");}return;}
    if(k=='r'||k=='R'){if(!busy){if(browse==Browse::Search)current_query=query_text();load_browse(true);}return;}
    if(k==LV_KEY_LEFT||k=='h'||k=='H'){int b=std::max(0,int(browse)-1);change_browse(static_cast<Browse>(b));return;}
    if(k==LV_KEY_RIGHT||k=='d'||k=='D'){int b=std::min(5,int(browse)+1);change_browse(static_cast<Browse>(b));return;}
    if(k==LV_KEY_UP||k=='w'||k=='W'||k=='k'||k=='K')change_selected(-1);
    else if(k==LV_KEY_DOWN||k=='s'||k=='S'||k=='j'||k=='J')change_selected(1);
    else if(k==LV_KEY_ENTER)open_selected();
}
}

int main(){
    signal(SIGINT,signal_stop);signal(SIGTERM,signal_stop);signal(SIGPIPE,SIG_IGN);mkdir((c1::data()+"/airtune").c_str(),0700);
    if(!screen::open())return 1;
    const std::string font_path="A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf";
    font_small=lv_tiny_ttf_create_file(font_path.c_str(),14);font=lv_tiny_ttf_create_file(font_path.c_str(),18);font_large=lv_tiny_ttf_create_file(font_path.c_str(),24);
    favorites_load();paint();status("Loading popular stations…");load_browse();
    uint32_t last_animation=0;
    while(!stopped&&!screen::quit){
        lv_timer_handler();for(uint32_t k;(k=screen::take_key());)key(k);
        uint32_t now=screen::tick();
        if(busy&&request_job.valid()&&request_job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){
            Json response=request_job.get();busy=false;auto done=std::move(request_done);request_done={};
            if(response.value("ok",false)){if(done)done(response.at("result"));}
            else {if(!content_loaded){content_loaded=true;paint();}status("Directory error · "+response.value("error",std::string("request failed")));}
            update_badge();
        }
        if(busy&&now-last_animation>=350){last_animation=now;static int phase=0;phase=(phase+1)%4;std::string dots(phase,'.');if(status_line)lv_label_set_text(status_line,(working_message+dots).c_str());if(badge_text)lv_label_set_text(badge_text,("LOAD"+dots).c_str());}
        if(connecting&&now-connecting_started>=6500){connecting=false;status("On air · "+(playing_index>=0&&playing_index<int(stations.size())?stations[playing_index].name:std::string("stream")));}
        if(player_pid>0&&waitpid(player_pid,nullptr,WNOHANG)==player_pid){player_pid=-1;connecting=false;if(player_in>=0){close(player_in);player_in=-1;}playing_index=-1;playing_station=Station{};update_player_panel();status("Stream ended · choose another station");}
        if(now-last_volume_poll>=500){last_volume_poll=now;update_volume();}
        if(category_list)update_player_panel();
        usleep(8000);
    }
    if(request_job.valid())request_job.wait();stop_player();lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);
    if(font_small)lv_tiny_ttf_destroy(font_small);if(font)lv_tiny_ttf_destroy(font);if(font_large)lv_tiny_ttf_destroy(font_large);
    if(audio_mixer)mixer_close(audio_mixer);screen::close();return 0;
}
