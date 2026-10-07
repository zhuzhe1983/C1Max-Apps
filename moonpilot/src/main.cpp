#include "text_input.hpp"
#include "agent.hpp"
#include "voice.hpp"
#include "remote.hpp"
#include "frame_image.hpp"
#include "process.hpp"
#include "display.hpp"
#include "idle_reset.h"
#include "lv_tiny_ttf.h"
#include <algorithm>
#include <csignal>
#include <filesystem>
#include <future>
#include <deque>
#include <optional>
#include <random>
#include <unistd.h>
#include <sys/stat.h>
using namespace moonpilot;
using c1input::ascii;
namespace {
c1ime::TextInput text_input;
constexpr uint32_t bg=0x111823,panel=0x1c2a3c,ink=0xeef5fa,muted=0xa7bac9,teal=0x9bbcff,red=0xfa8e88;
constexpr int preview_w=488,preview_h=250;
enum class Page{Agent,Hosts,Voice,Settings};Page page=Page::Hosts;int settings_tab=0;
Settings vision,chat,asr,tts;moonpilot::Host host;moonpilot::Remote remote;
std::vector<moonpilot::Application>applications;int app_id=0,app_index=0;
Process recorder,speaker;
bool busy=false,cancelled=false,running=false,executing=false,recording=false,transcribe_next=false,spoken=true;
bool connection_job=false,manual=false;std::atomic<bool>network_cancel{false};
int steps=0,remaining=0;uint32_t next_step=0,started_recording=0,last_state=0;uint64_t observed_sequence=0,painted_sequence=0;
std::atomic<int> voice_stage{0};int displayed_stage=0;
std::string heard,goal,notice="先添加 Sunshine 主机并完成配对",reply,pin;
Json actions_history=Json::array(),chat_history=Json::array();
std::future<Json>job;std::function<void(Json)>completion;
std::deque<std::function<void()>>callbacks;
lv_font_t*font=nullptr,*small=nullptr,*title=nullptr;
lv_obj_t*status_label=nullptr,*connection_label=nullptr,*image=nullptr,*task_field=nullptr,*focused=nullptr,*fields[3]{},*record_label=nullptr,*app_label=nullptr;
std::vector<uint32_t>preview(preview_w*preview_h,0xff0a1015);lv_image_dsc_t descriptor{};
volatile sig_atomic_t quitting=0;void quit_signal(int){quitting=1;}
std::string path(const char*name){return c1::data()+"/moonpilot/"+name;}
void paint();void stop_task();void request_step();void begin_record();void speak(const std::string&);void set_page(Page);
void message(const std::string&s){notice=s;if(status_label)lv_label_set_text(status_label,s.c_str());}
lv_obj_t*box(lv_obj_t*p,int x,int y,int w,int h,uint32_t color=panel){auto*o=lv_obj_create(p);lv_obj_remove_style_all(o);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(o,lv_color_hex(color),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);lv_obj_set_style_radius(o,10,0);return o;}
lv_obj_t*text(lv_obj_t*p,const std::string&s,int x,int y,int w,int h=26,uint32_t color=ink,lv_font_t*f=nullptr){auto*o=lv_label_create(p);lv_label_set_text(o,s.c_str());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_label_set_long_mode(o,LV_LABEL_LONG_DOT);lv_obj_set_style_text_color(o,lv_color_hex(color),0);if(f)lv_obj_set_style_text_font(o,f,0);return o;}
lv_obj_t*button(const std::string&s,int x,int y,int w,std::function<void()>fn,bool active=false){auto*o=box(lv_screen_active(),x,y,w,44,active?teal:panel);lv_obj_add_flag(o,LV_OBJ_FLAG_CLICKABLE);lv_obj_set_style_bg_color(o,lv_color_hex(0x346b72),LV_STATE_PRESSED);callbacks.push_back(std::move(fn));lv_obj_add_event_cb(o,[](lv_event_t*e){auto fn=*static_cast<std::function<void()>*>(lv_event_get_user_data(e));try{fn();}catch(const std::exception&x){message(x.what());}},LV_EVENT_CLICKED,&callbacks.back());auto*l=text(o,s,5,10,w-10,25,active?0x102b31:ink);lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);return o;}
lv_obj_t*field(const std::string&value,int x,int y,int w,bool password=false){auto*o=lv_textarea_create(lv_screen_active());lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,44);lv_textarea_set_one_line(o,true);lv_textarea_set_max_length(o,512);lv_textarea_set_password_mode(o,password);lv_textarea_set_text(o,value.c_str());lv_obj_set_style_bg_color(o,lv_color_hex(panel),0);lv_obj_set_style_text_color(o,lv_color_hex(ink),0);lv_obj_set_style_border_color(o,lv_color_hex(teal),LV_STATE_FOCUSED);lv_obj_set_style_pad_all(o,9,0);lv_obj_add_event_cb(o,[](lv_event_t*e){auto*o=(lv_obj_t*)lv_event_get_target(e);if(focused&&focused!=o)lv_obj_remove_state(focused,LV_STATE_FOCUSED);focused=o;lv_obj_add_state(o,LV_STATE_FOCUSED);},LV_EVENT_CLICKED,nullptr);return o;}
Settings&service(){return settings_tab==0?vision:settings_tab==1?chat:settings_tab==2?asr:tts;}
Json config(){auto encode=[](const Settings&s){return Json{{"endpoint",s.endpoint},{"model",s.model},{"token",s.token}};};return {{"vision",encode(vision)},{"chat",encode(chat)},{"asr",encode(asr)},{"tts",encode(tts)},{"spoken",spoken},{"host",{{"address",host.address},{"port",host.port},{"width",host.width},{"height",host.height},{"fps",host.fps},{"bitrate",host.bitrate},{"app_id",app_id}}}};}
void save_settings(){if(page==Page::Settings&&fields[0]){auto&s=service();s.endpoint=lv_textarea_get_text(fields[0]);s.model=lv_textarea_get_text(fields[1]);s.token=lv_textarea_get_text(fields[2]);if(!s.endpoint.empty())validate_settings(s);}c1::save_private(path("settings.json"),config().dump(2));}
void save_host(){if(page!=Page::Hosts||!fields[0])return;auto next=host;next.address=lv_textarea_get_text(fields[0]);std::string port=lv_textarea_get_text(fields[1]);if(port.empty()||port.size()>5||port.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("请输入有效端口，默认 47989");next.port=std::stoi(port);moonpilot::validate_host(next);if(next.address!=host.address||next.port!=host.port){if(remote.active())throw std::runtime_error("请先断开当前主机");applications.clear();app_id=app_index=0;}host=next;save_settings();}
void work(const std::string&label,std::function<Json()>fn,std::function<void(Json)>done){if(busy)throw std::runtime_error("请先等待或停止当前任务");busy=true;cancelled=false;voice_stage=displayed_stage=0;c1::reset_requests(95000);message(label);completion=std::move(done);job=std::async(std::launch::async,[fn]{try{return Json{{"ok",true},{"data",fn()}};}catch(const std::exception&e){return Json{{"ok",false},{"error",e.what()}};}});}
void stop_task(){
    running=executing=false;manual=false;if(busy){cancelled=true;c1::cancel_requests();network_cancel=true;if(connection_job)remote.cancel();}
    recorder.stop();speaker.stop();recording=transcribe_next=false;if(!connection_job)remote.release();unlink(path("record.wav").c_str());message("已停止，远程按键已释放");
}
void set_page(Page next){
    if(task_field&&page==Page::Agent)goal=lv_textarea_get_text(task_field);
    if(page==Page::Settings)save_settings();
    if(page==Page::Hosts&&fields[0]&&*lv_textarea_get_text(fields[0]))save_host();
    stop_task();page=next;paint();
}
void disconnect(){stop_task();if(!connection_job)remote.stop();message("已断开，电脑上的应用保持运行");}
void inspect_host(bool pair){
    if(busy)throw std::runtime_error("请先等待或停止当前任务");save_host();network_cancel=false;
    if(pair){std::random_device random;pin=std::to_string(1000+random()%9000);}
    const auto selected=host;const auto code=pin;
    work(pair?"请在 Sunshine 的 PIN 页面输入："+code:"正在检查主机…",[selected,code,pair]{
        moonpilot::GameStream gs(selected,c1::data()+"/moonpilot",network_cancel);auto info=pair?gs.pair(code):gs.inspect();Json apps=Json::array();if(info.paired)for(auto&a:gs.applications())apps.push_back({{"id",a.id},{"name",a.name}});
        return Json{{"paired",info.paired},{"name",info.name},{"apps",apps}};
    },[](Json out){applications.clear();app_index=0;for(auto&a:out["apps"])applications.push_back({a.at("id"),a.at("name")});
        for(size_t i=0;i<applications.size();i++)if(applications[i].id==app_id||(app_id==0&&applications[i].name=="Desktop"))app_index=i;
        if(!applications.empty())app_id=applications[app_index].id;save_settings();message(out.value("paired",false)?"已配对 · 选择桌面或应用，然后连接":"发现主机 · 点配对并在 Sunshine 输入 PIN");paint();
    });
}
void connect_host(){
    if(busy)throw std::runtime_error("请先等待或停止当前任务");if(remote.active()){disconnect();return;}save_host();if(app_id<1)throw std::runtime_error("请先检查主机并选择桌面或应用");
    auto selected=host;int app=app_id;network_cancel=false;connection_job=true;
    work("正在连接远程桌面…",[selected,app]{remote.start(selected,app,c1::data()+"/moonpilot",c1::root(),network_cancel);return Json{};},[](Json){page=Page::Agent;message("已连接 · 等待桌面画面");paint();});
}
void update_preview(){
    if(!image||connection_job)return;const auto&f=remote.frame();if(f.rgb.empty())return;
    std::fill(preview.begin(),preview.end(),0xff0a1015);double scale=std::min(double(preview_w)/f.width,double(preview_h)/f.height);int w=f.width*scale,h=f.height*scale,x0=(preview_w-w)/2,y0=(preview_h-h)/2;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++)preview[(y+y0)*preview_w+x+x0]=f.rgb[std::min(f.height-1,int(y/scale))*f.width+std::min(f.width-1,int(x/scale))];
    descriptor.header.magic=LV_IMAGE_HEADER_MAGIC;descriptor.header.cf=LV_COLOR_FORMAT_ARGB8888;descriptor.header.w=preview_w;descriptor.header.h=preview_h;descriptor.header.stride=preview_w*4;descriptor.data_size=preview.size()*4;descriptor.data=(uint8_t*)preview.data();lv_image_set_src(image,&descriptor);lv_obj_invalidate(image);painted_sequence=f.sequence;
}
void manual_tap(lv_event_t*){
    if(!manual||busy||!remote.ready())return;const auto&f=remote.frame();if(f.rgb.empty()||screen::tick()-f.time>2000)return;
    lv_point_t p;lv_indev_get_point(lv_indev_active(),&p);double scale=std::min(double(preview_w)/f.width,double(preview_h)/f.height);int w=f.width*scale,h=f.height*scale,x0=12+(preview_w-w)/2,y0=56+(preview_h-h)/2;
    if(p.x<x0||p.y<y0||p.x>=x0+w||p.y>=y0+h)return;
    try{remote.move(double(p.x-x0)/std::max(1,w-1),double(p.y-y0)/std::max(1,h-1));remote.click();}catch(const std::exception&e){message(e.what());}
}
void apply(const Action&a){
    if(!remote.ready())throw std::runtime_error("远程桌面已断开，动作未执行");
    if(a.kind=="click"||a.kind=="double_click"||a.kind=="move"){remote.move(a.x,a.y);if(a.kind!="move")remote.click(a.button,a.kind=="double_click");}
    else if(a.kind=="type")remote.type(a.text);else if(a.kind=="key")remote.key(a.key,a.mods);else if(a.kind=="scroll")remote.scroll(a.amount);
    executing=true;next_step=screen::tick()+(a.kind=="wait"?a.ms:750);observed_sequence=remote.frame().sequence;steps++;remaining--;
    actions_history.push_back({{"action",a.kind},{"summary",a.summary}});while(actions_history.size()>10)actions_history.erase(actions_history.begin());message("第 "+std::to_string(steps)+" 步 · "+describe(a));
}
void request_step(){
    if(busy||!running)return;const auto&f=remote.frame();if(!remote.ready()||f.rgb.empty()||screen::tick()-f.time>2000){stop_task();message("桌面画面尚未就绪或已过期，请重连后再试");return;}
    auto bytes=jpeg(f.rgb,f.width,f.height);auto settings=vision;auto task=goal;auto history=actions_history;
    work("观察桌面，等待模型…",[settings,task,bytes,history]{auto a=decide(settings,task,bytes,history);return Json{{"kind",a.kind},{"summary",a.summary},{"text",a.text},{"x",a.x},{"y",a.y},{"amount",a.amount},{"ms",a.ms},{"key",a.key},{"mods",a.mods},{"button",a.button}};},[](Json j){
        Action a;a.kind=j.at("kind");a.summary=j.at("summary");a.text=j.at("text");a.x=j.at("x");a.y=j.at("y");a.amount=j.at("amount");a.ms=j.at("ms");a.key=j.at("key");a.mods=j.at("mods");a.button=j.at("button");
        if(!remote.ready()||screen::tick()-remote.frame().time>2000)throw std::runtime_error("等待模型时桌面已断开，动作未执行");
        if(a.kind=="done"||a.kind=="ask"){running=false;message(describe(a));reply=describe(a);if(spoken&&!tts.endpoint.empty())speak(reply);return;}apply(a);
    });
}
void start_task(int count){
    if(busy||recording)throw std::runtime_error("请先停止当前请求或录音");if(task_field)goal=lv_textarea_get_text(task_field);if(goal.empty())throw std::runtime_error("请输入任务，或通过语音对话生成任务");validate_settings(vision);
    if(!remote.ready())throw std::runtime_error("请先连接 Sunshine 远程桌面");manual=false;focused=nullptr;if(task_field)lv_obj_remove_state(task_field,LV_STATE_FOCUSED);steps=0;remaining=count;actions_history=Json::array();running=true;request_step();
}
void text_chat();
void play_wav(){speaker.start({"/usr/bin/mplayer","-noconfig","all","-quiet","-noconsolecontrols","-nolirc","-nojoystick","-nomouseinput","-vo","null","-ao","media",path("reply.wav")},path("audio.log"));}
void speak(const std::string&s){if(busy||s.empty()||tts.endpoint.empty())return;auto settings=tts;work("合成语音…",[settings,s]{auto wav=synthesize(settings,s);c1::save_private(path("reply.wav"),wav);return Json{};},[](Json){play_wav();message(reply);});}
void begin_record(){
    if(recording){recorder.finish_recording();transcribe_next=true;message("结束录音，正在准备识别…");return;}
    if(busy||running)throw std::runtime_error("请先停止当前任务");validate_settings(asr);validate_settings(chat);speaker.stop();unlink(path("record.wav").c_str());
    recorder.start({"/usr/bin/arecord","-q","-D","plughw:0,1","-f","S16_LE","-r","16000","-c","1","-t","wav","-d","10",path("record.wav")},path("record.log"));
    recording=transcribe_next=true;started_recording=screen::tick();message("录音中 · 再按语音结束，最长 10 秒");
}
void voice_done(Json out){heard=out.at("heard");reply=out.at("reply");chat_history.push_back({{"role","user"},{"content",out.at("heard")}});chat_history.push_back({{"role","assistant"},{"content",reply}});while(chat_history.size()>6)chat_history.erase(chat_history.begin());
        if(out.contains("task")&&!out["task"].is_null()){goal=out["task"];if(task_field&&page==Page::Agent)lv_textarea_set_text(task_field,goal.c_str());}
        if(page==Page::Voice)paint();message(out.contains("voice_error")?"回答已显示 · "+out["voice_error"].get<std::string>():"回答已显示");if(out.value("audio",false))play_wav();
}
void text_chat(){
    if(busy||recording||running)throw std::runtime_error("请先停止当前任务");
    auto input=std::string(lv_textarea_get_text(task_field));if(input.empty())throw std::runtime_error("请输入文字");
    auto settings=chat,voice=tts;auto history=chat_history;bool read_aloud=spoken;
    work("正在对话…",[settings,voice,history,input,read_aloud]{voice_stage=2;auto out=converse(settings,input,history);out["heard"]=input;
        if(read_aloud&&!voice.endpoint.empty())try{voice_stage=3;auto audio=synthesize(voice,out.at("reply"));c1::save_private(path("reply.wav"),audio);out["audio"]=true;}catch(const std::exception&e){out["voice_error"]=e.what();}return out;
    },voice_done);
}
void finish_voice(){
    recording=transcribe_next=false;std::string wav=c1::read_file(path("record.wav"),700000);unlink(path("record.wav").c_str());auto recognition=asr,conversation=chat,voice=tts;bool read_aloud=spoken;auto history=chat_history;
    work("正在识别语音…",[recognition,conversation,voice,read_aloud,wav,history]{voice_stage=1;auto result=transcribe(recognition,wav);voice_stage=2;auto out=converse(conversation,result,history);out["heard"]=result;
        if(read_aloud&&!voice.endpoint.empty())try{voice_stage=3;auto audio=synthesize(voice,out.at("reply"));c1::save_private(path("reply.wav"),audio);out["audio"]=true;}catch(const std::exception&e){out["voice_error"]=e.what();}return out;
    },voice_done);
}
void paint(){
    auto*r=lv_screen_active();lv_obj_clean(r);callbacks.clear();focused=task_field=status_label=image=record_label=connection_label=app_label=nullptr;for(auto&f:fields)f=nullptr;
    lv_obj_remove_flag(r,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(r,lv_color_hex(bg),0);lv_obj_set_style_bg_opa(r,LV_OPA_COVER,0);lv_obj_set_style_text_font(r,font?font:LV_FONT_DEFAULT,0);lv_obj_set_style_text_color(r,lv_color_hex(ink),0);
    text(r,"MoonPilot",14,13,148,32,ink,title);
    button("桌面",166,5,78,[]{set_page(Page::Agent);},page==Page::Agent);button("主机",252,5,78,[]{set_page(Page::Hosts);},page==Page::Hosts);button("语音",338,5,78,[]{set_page(Page::Voice);},page==Page::Voice);button("设置",424,5,78,[]{set_page(Page::Settings);},page==Page::Settings);
    auto*b=button("尚未连接",522,5,264,[]{if(remote.active()||connection_job)disconnect();else set_page(Page::Hosts);});connection_label=lv_obj_get_child(b,0);
    if(page==Page::Hosts){
        text(r,"电脑 IP 地址",16,60,535,26,muted,small);text(r,"HTTP 端口",568,60,216,26,muted,small);
        fields[0]=field(host.address,14,86,538);lv_textarea_set_max_length(fields[0],45);lv_textarea_set_placeholder_text(fields[0],"Sunshine 所在电脑的局域网 IP");fields[1]=field(std::to_string(host.port),568,86,218);lv_textarea_set_max_length(fields[1],5);lv_textarea_set_accepted_chars(fields[1],"0123456789");
        button("检查主机",14,144,142,[]{inspect_host(false);});button("PIN 配对",164,144,142,[]{inspect_host(true);});button("连接桌面",314,144,142,[]{connect_host();},true);
        button("停止",464,144,94,[]{disconnect();});
        button(host.width==512?"画质：省流":host.width==640?"画质：标准":"画质：清晰",568,144,218,[]{
            if(busy||remote.active())throw std::runtime_error("请先断开连接再调整画质");save_host();if(host.width==512){host.width=640;host.height=360;host.bitrate=1500;}else if(host.width==640){host.width=800;host.height=450;host.bitrate=2200;}else{host.width=512;host.height=288;host.bitrate=900;}save_settings();paint();
        });
        text(r,"串流内容",16,206,116,28,muted,small);app_label=text(r,applications.empty()?(app_id?"已保存应用 #"+std::to_string(app_id):"检查或配对后加载应用列表"):applications[app_index].name,136,202,500,36,ink,font);
        button("切换",654,197,132,[]{if(busy||remote.active()||applications.empty())return;app_index=(app_index+1)%applications.size();app_id=applications[app_index].id;save_settings();paint();});
        status_label=text(r,notice,16,253,770,70,ink,font);lv_label_set_long_mode(status_label,LV_LABEL_LONG_WRAP);return;
    }
    if(page==Page::Settings){
        static const char*names[]={"视觉","对话","识别","播报"};for(int i=0;i<4;i++)button(names[i],14+i*119,62,111,[i]{if(busy)return;save_settings();settings_tab=i;paint();},settings_tab==i);
        auto&s=service();text(r,"接口地址",16,126,130,26,muted);fields[0]=field(s.endpoint,152,116,634);text(r,"模型名称",16,186,130,26,muted);fields[1]=field(s.model,152,176,634);text(r,"API 密钥",16,246,130,26,muted);fields[2]=field(s.token,152,236,634,true);
        button("保存",654,62,132,[]{save_settings();message("设置已保存");});button(spoken?"播报：开":"播报：关",498,62,148,[]{spoken=!spoken;save_settings();paint();},spoken);status_label=text(r,notice,16,291,770,39,muted,small);return;
    }
    if(page==Page::Voice){
        auto*conversation=box(r,12,57,488,245);lv_obj_add_flag(conversation,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_scroll_dir(conversation,LV_DIR_VER);
        std::string transcript=heard.empty()?"按开始录音说话，或在右侧输入文字。\n\n操作请求会填入桌面任务栏，由你按运行开始。":("你："+heard+"\n\nMoonPilot："+reply);
        auto*l=text(conversation,transcript,16,14,456,1,ink,font);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);lv_obj_set_height(l,LV_SIZE_CONTENT);
        text(r,"文字对话",516,58,270,25,muted,small);task_field=field("",516,82,270);lv_textarea_set_placeholder_text(task_field,"点击后拍摄键中文");
        button("发送",516,137,130,[]{text_chat();});button("查看任务",654,137,132,[]{focused=nullptr;task_field=nullptr;set_page(Page::Agent);});
        b=button("开始录音",516,203,270,[]{begin_record();},true);record_label=lv_obj_get_child(b,0);button("再读一遍",516,257,130,[]{speak(reply);});button("停止",654,257,132,[]{stop_task();});status_label=text(r,notice,14,312,770,23,muted,small);return;
    }
    box(r,12,56,preview_w,preview_h,0x0a1015);
    if(connection_job||remote.frame().rgb.empty()){text(r,"远程桌面",34,125,440,35,teal,title);text(r,"在「主机」里添加电脑并连接 Sunshine",34,177,440,58,muted,font);}
    image=lv_image_create(r);lv_obj_set_pos(image,12,56);lv_obj_set_size(image,preview_w,preview_h);lv_obj_add_flag(image,LV_OBJ_FLAG_CLICKABLE);lv_obj_add_event_cb(image,manual_tap,LV_EVENT_CLICKED,nullptr);
    text(r,"任务",516,57,270,25,muted,small);task_field=field(goal,516,82,270);lv_textarea_set_placeholder_text(task_field,"任务（拍摄键中文）");
    status_label=text(r,notice,516,136,270,59,ink,small);lv_label_set_long_mode(status_label,LV_LABEL_LONG_WRAP);
    button("单步",516,203,130,[]{start_task(1);});button("运行 10 步",654,203,132,[]{start_task(10);},true);
    button(manual?"手动：开":"手动",516,257,80,[]{if(busy)throw std::runtime_error("请先停止模型请求");running=executing=false;manual=!manual;if(task_field)goal=lv_textarea_get_text(task_field);message(manual?"点击画面控制鼠标 · 实体键盘发给电脑":"已关闭手动输入");paint();},manual);
    button("语音",604,257,84,[]{set_page(Page::Voice);});button("停止",696,257,90,[]{stop_task();});
    text(r,"返回键停止操作 · 电源键返回菜单 · 完整画面按比例显示",14,312,770,23,muted,small);update_preview();
}
void key(uint32_t k){
    if(text_input.key(k))return;
    if(k==screen::KEY_HOME){screen::quit=true;return;}
    if(k==screen::KEY_SYMBOL&&focused&&!busy&&(focused==task_field||(page==Page::Settings&&focused==fields[1]))){
        bool task=focused==task_field;auto origin=page;
        text_input.open("moonpilot",task?"任务 / 对话草稿":"模型名称",lv_textarea_get_text(focused),512,font,[task,origin](std::string value){
            if(page!=origin)return;auto*o=task?task_field:fields[1];if(o){lv_textarea_set_text(o,value.c_str());focused=o;lv_obj_add_state(o,LV_STATE_FOCUSED);if(task&&page==Page::Agent)goal=value;}message("中文已填入 · 尚未发送或执行");
        });return;
    }
    if(k==screen::KEY_EXIT){stop_task();if(focused){lv_obj_remove_state(focused,LV_STATE_FOCUSED);focused=nullptr;}else if(page!=Page::Agent)set_page(Page::Agent);return;}
    if(focused&&!busy){if(k==LV_KEY_ENTER){if(focused==task_field&&page==Page::Voice){text_chat();return;}if(page==Page::Hosts)save_host();else if(page==Page::Settings)save_settings();else goal=lv_textarea_get_text(task_field);lv_obj_remove_state(focused,LV_STATE_FOCUSED);focused=nullptr;}else if(k==LV_KEY_BACKSPACE)lv_textarea_delete_char(focused);else if(k>=32&&k<127){char c[2]={char(k),0};lv_textarea_add_text(focused,c);}return;}
    if(k==screen::KEY_MODE){message(screen::caps_lock()?"ABC 大写":"abc 小写 · Shift 符号");return;}
    if(page==Page::Agent&&manual&&!busy){if(k==LV_KEY_ENTER)remote.key(40);else if(k==LV_KEY_BACKSPACE)remote.key(42);else if(k>=32&&k<127){auto a=ascii(k);remote.key(a[0],a[1]);}return;}
    if((page==Page::Agent||page==Page::Voice)&&k==screen::KEY_SYMBOL){if(page==Page::Agent)set_page(Page::Voice);begin_record();}
}
void load(){
    std::filesystem::create_directories(c1::data()+"/moonpilot");Json j=Json::object();
    try{j=Json::parse(c1::read_file(path("settings.json"),16384));}catch(...){
        // One-time migration; never overwrite an existing MoonPilot profile.
        if(!std::filesystem::exists(path("settings.json")))try{j=Json::parse(c1::read_file(c1::data()+"/hidpilot/settings.json",8192));c1::save_private(path("settings.json"),j.dump(2));}catch(...){}
    }
    try{auto decode=[&](const char*k,Settings&s){if(j.contains(k)){auto&a=j[k];s={a.value("endpoint",""),a.value("model",""),a.value("token","")};}};decode("vision",vision);decode("chat",chat);decode("asr",asr);decode("tts",tts);spoken=j.value("spoken",true);
        if(j.contains("host")){auto&h=j["host"];moonpilot::Host next;next.address=h.value("address","");next.port=h.value("port",47989);next.width=h.value("width",640);next.height=h.value("height",360);next.fps=h.value("fps",15);next.bitrate=h.value("bitrate",1500);if(!next.address.empty())moonpilot::validate_host(next);host=next;app_id=h.value("app_id",0);}
    }catch(...){notice="设置中有无效字段，请检查配置";}
}
}
int main(){
    signal(SIGINT,quit_signal);signal(SIGTERM,quit_signal);signal(SIGPIPE,SIG_IGN);umask(0077);load();if(!screen::open())return 1;
    auto fp="A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf";font=lv_tiny_ttf_create_file(fp.c_str(),18);small=lv_tiny_ttf_create_file(fp.c_str(),16);title=lv_tiny_ttf_create_file(fp.c_str(),24);for(auto*f:{font,small,title})if(f)f->fallback=&lv_font_montserrat_18;
    c1_reset_idle();uint32_t idle_at=screen::tick();paint();
    while(!screen::quit&&!quitting){
        lv_timer_handler();uint32_t now=screen::tick();if(!connection_job)remote.poll();
        if((busy||running||recording||speaker.active()||remote.active())&&now-idle_at>=5000){c1_reset_idle();idle_at=now;}
        for(uint32_t k;(k=screen::take_key());)try{key(k);}catch(const std::exception&e){message(e.what());}
        if(!connection_job&&image&&remote.frame().sequence!=painted_sequence)update_preview();
        if(recording){if(record_label)lv_label_set_text(record_label,("结束 "+std::to_string((now-started_recording)/1000)).c_str());if(recorder.poll())try{if(transcribe_next)finish_voice();}catch(const std::exception&e){recording=transcribe_next=false;message(e.what());}}
        else if(record_label)lv_label_set_text(record_label,"开始录音");
        if(busy&&voice_stage.load()!=displayed_stage){displayed_stage=voice_stage.load();message(displayed_stage==1?"正在识别语音…":displayed_stage==2?"正在对话…":displayed_stage==3?"正在合成语音…":notice);}
        speaker.poll();
        if(busy&&job.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto out=job.get();busy=false;bool connection=connection_job;connection_job=false;auto done=std::move(completion);
            if(cancelled){if(connection)remote.stop();c1::reset_requests();message("已停止");}
            else if(out.value("ok",false))try{done(out.at("data"));}catch(const std::exception&e){running=executing=false;remote.release();message(e.what());}
            else{running=executing=false;remote.release();message(out.value("error","请求失败"));}}
        if(!connection_job&&remote.active()&&!remote.error().empty()){auto e=remote.error();stop_task();remote.stop();message(e);}
        if(executing&&int32_t(now-next_step)>=0){
            if(remote.frame().sequence>observed_sequence){executing=false;if(remaining>0&&running)next_step=now+100;else{running=false;message("本轮已执行 "+std::to_string(steps)+" 步 · 可继续单步或运行");}}
            else if(now-next_step>5000){stop_task();message("操作后未收到新画面，已停止任务");}
        }
        if(running&&!busy&&!executing&&int32_t(now-next_step)>=0)try{request_step();}catch(const std::exception&e){stop_task();message(e.what());}
        if(now-last_state>200){last_state=now;if(connection_label)lv_label_set_text(connection_label,connection_job?"正在连接… · 点此取消":remote.ready()?"已连接 · 点此断开":"选择 Sunshine 主机");}
        usleep(5000);
    }
    stop_task();network_cancel=true;remote.cancel();c1::cancel_requests();if(job.valid())job.wait();remote.stop();speaker.stop();recorder.stop();c1_reset_idle();unlink(path("reply.wav").c_str());unlink(path("record.wav").c_str());
    text_input.close();lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);for(auto*f:{font,small,title})if(f)lv_tiny_ttf_destroy(f);screen::close();return 0;
}
