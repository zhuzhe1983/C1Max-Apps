#define main input_app_main
#if INPUT_APP == 1
#include "../streamplayer/src/main.cpp"
#elif INPUT_APP == 2
#include "../bilibili/src/main.cpp"
#elif INPUT_APP == 3
#include "../calendar/src/main.cpp"
#elif INPUT_APP == 4
#include "../mail/src/main.cpp"
#elif INPUT_APP == 5
#include "../settings/src/main.cpp"
#elif INPUT_APP == 6
#include "../tox/src/main.cpp"
#elif INPUT_APP == 7
#include "../moonpilot/src/main.cpp"
#endif
#undef main
#include <cassert>
#include "net.hpp"
static void input_key(uint32_t k){
#if INPUT_APP == 1 || INPUT_APP == 3 || INPUT_APP == 5
 physical_key(k);
#else
 key(k);
#endif
}
static void hello(){for(char c:std::string("nihao"))input_key(c);input_key(LV_KEY_ENTER);assert(text_input.text()=="你好"&&text_input.active());input_key(LV_KEY_ENTER);assert(!text_input.active());}
int main(int argc,char**argv){
 assert(argc==3);setenv("C1_APPS_ROOT",argv[1],1);setenv("C1_APPS_DATA",argv[2],1);assert(screen::open());
 auto*test_font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),18);assert(test_font);font=test_font;
#if INPUT_APP == 1
 client.config={{"base","http://127.0.0.1:1"},{"user_id","test"},{"token","test"}};
 libraries=Json::array({{{"Id","music"},{"Name","音乐"},{"CollectionType","music"}}});rows=Json::array();parent_id="music";render_items();input_key('f');assert(text_input.active());
 c1::reset_requests(200);hello();assert(search_term=="你好"&&page==0);if(job.valid())job.get();busy=false;completed={};
 show_setup();active_field=username;lv_textarea_set_text(username,"");input_key(screen::KEY_SYMBOL);hello();assert(std::string(lv_textarea_get_text(username))=="你好");
#elif INPUT_APP == 2
 small=large=font;page=Page::Search;paint();lv_obj_add_state(field,LV_STATE_FOCUSED);input_key('w');input_key('s');assert(std::string(lv_textarea_get_text(field))=="ws");lv_textarea_set_text(field,"");input_key(screen::KEY_SYMBOL);
 paint();assert(text_input.active());hello();assert(query=="你好"&&std::string(lv_textarea_get_text(field))=="你好"&&!busy);paint();assert(lv_obj_has_state(field,LV_STATE_FOCUSED));
 page=Page::Saved;store["saved"]=Json::array({{{"bvid","BV1111111111"},{"title","你好一"}},{{"bvid","BV2222222222"},{"title","Other"}},{{"bvid","BV3333333333"},{"title","你好二"}}});local_query="你好";offset=selected=0;load_list();assert(items.size()==2&&!busy);input_key(LV_KEY_BACKSPACE);assert(store["saved"].size()==2&&items.size()==1&&items[0]["title"]=="你好二");
#elif INPUT_APP == 3
 selected={2026,10,7};month={2026,10,1};show_edit();for(size_t i=0;i<focus.size();++i)if(focus[i].obj==fields[0]){set_focus(i);break;}
 input_key(screen::KEY_SYMBOL);hello();assert(std::string(lv_textarea_get_text(fields[0]))=="你好"&&draft_text[0]=="你好");
 for(size_t i=0;i<focus.size();++i)if(focus[i].obj==fields[1]){set_focus(i);break;}input_key(screen::KEY_SYMBOL);assert(!text_input.active());
#elif INPUT_APP == 4
 page=Page::Compose;compose_selected=0;editing=false;edit_value.clear();paint();input_key('j');assert(compose_selected==1);input_key(LV_KEY_ENTER);assert(editing);input_key(screen::KEY_SYMBOL);hello();assert(edit_value=="你好"&&subject.empty()&&editing);finish_edit();assert(subject=="你好"&&page==Page::Compose);
#elif INPUT_APP == 5
 small=font;sheet=Sheet::Hidden;zone=Zone::Content;input_target=1;create_ui();sync_list(true);input_key(screen::KEY_SYMBOL);hello();assert(hidden_ssid=="你好"&&sheet==Sheet::Hidden);input_key(LV_KEY_BACKSPACE);assert(hidden_ssid=="你");
#elif INPUT_APP == 6
 page=Page::Rename;view.ready=true;edit.clear();paint();input_key(screen::KEY_SYMBOL);hello();assert(edit=="你好"&&pending==0&&page==Page::Rename);
 page=Page::Chat;selected=7;drafts[7]="";input_key(screen::KEY_SYMBOL);hello();assert(drafts[7]=="你好"&&pending==0&&!media);
#elif INPUT_APP == 7
 small=title=font;page=Page::Voice;paint();focused=task_field;input_key(screen::KEY_SYMBOL);hello();assert(std::string(lv_textarea_get_text(task_field))=="你好"&&!busy&&!recording&&!executing);
#endif
 text_input.close();lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);lv_tiny_ttf_destroy(test_font);font=nullptr;screen::close();puts("PASS app Pinyin routing and draft-only confirmation");
}
