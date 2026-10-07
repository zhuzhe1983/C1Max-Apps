#define main airtune_app_main
#include "../src/main.cpp"
#undef main
#include <cassert>
int main(){
    assert(screen::open());stations={{"One","","http://example.invalid/one","one",64},{"Two","","http://example.invalid/two","two",64}};content_loaded=true;paint();
    lv_obj_send_event(query,LV_EVENT_CLICKED,nullptr);assert(query_mode);
    key('w');key('s');assert(query_text()=="ws"&&selected==0);
    busy=true;key('s');assert(query_text()=="wss"&&selected==0);key(LV_KEY_ENTER);assert(query_mode&&browse==Browse::Popular);
    paint();assert(query_text()=="wss"&&query_mode);busy=false;key(LV_KEY_BACKSPACE);assert(query_text()=="ws");
    key(screen::KEY_EXIT);assert(!query_mode);key('s');assert(selected==1);key('w');assert(selected==0);
    key('q');assert(query_mode&&query_text().empty());key('s');key('w');key('f');key('r');assert(query_text()=="swfr"&&selected==0);
    lv_refr_now(nullptr);lv_obj_clean(lv_screen_active());query=nullptr;screen::close();puts("PASS Airtune: touch/Q focus, W/S text, async repaint, busy input, deletion, cancel and navigation");
}
