#define main airtune_app_main
#include "../src/main.cpp"
#undef main
#include <cassert>
int main(int argc,char**argv){
    assert(argc==3);setenv("C1_APPS_ROOT",argv[1],1);setenv("C1_APPS_DATA",argv[2],1);
    assert(screen::open());stations={{"One","","http://example.invalid/one","one",64},{"Two","","http://example.invalid/two","two",64}};content_loaded=true;paint();
    lv_obj_send_event(query,LV_EVENT_CLICKED,nullptr);assert(query_mode);
    key('w');key('s');assert(query_text()=="ws"&&selected==0);
    busy=true;key('s');assert(query_text()=="wss"&&selected==0);key(LV_KEY_ENTER);assert(query_mode&&browse==Browse::Popular);
    paint();assert(query_text()=="wss"&&query_mode);busy=false;key(LV_KEY_BACKSPACE);assert(query_text()=="ws");
    key(screen::KEY_EXIT);assert(!query_mode);key('s');assert(selected==1);key('w');assert(selected==0);
    key('q');assert(query_mode&&query_text().empty());key('s');key('w');key('f');key('r');assert(query_text()=="swfr"&&selected==0);
    key(screen::KEY_EXIT);browse=Browse::Saved;favorites=default_stations();stations=saved_stations_view();paint();key('q');key(screen::KEY_SYMBOL);assert(text_input.active());
    for(char c:std::string("beijing"))key(c);key(LV_KEY_ENTER);assert(text_input.text()=="北京");
    key(LV_KEY_ENTER);assert(query_mode&&query_text()=="北京");key(LV_KEY_ENTER);
    assert(browse==Browse::Saved&&!busy&&stations.size()==4&&stations[0].name=="北京音乐广播");
    key('q');key('s');key('w');key(LV_KEY_ENTER);assert(stations.empty()&&!busy);
    saved_query.clear();begin_add_station();key(screen::KEY_SYMBOL);for(char c:std::string("nihao"))key(c);key(LV_KEY_ENTER);key(LV_KEY_ENTER);assert(query_text()=="你好"&&add_name=="你好");
    key(LV_KEY_ENTER);assert(add_step==1);key(screen::KEY_SYMBOL);assert(!text_input.active());key(screen::KEY_EXIT);
    lv_refr_now(nullptr);lv_obj_clean(lv_screen_active());query=nullptr;screen::close();puts("PASS Airtune: touch/Q focus, W/S text, async repaint, busy input, deletion, cancel and navigation");
}
