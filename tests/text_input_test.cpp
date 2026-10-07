#include "text_input.hpp"
#include "display.hpp"
#include "net.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <cassert>
#include <filesystem>
int main(int argc,char**argv){
    assert(argc==3);setenv("C1_APPS_ROOT",argv[1],1);setenv("C1_APPS_DATA",argv[2],1);assert(screen::open());
    auto*font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),18);assert(font);
    c1ime::TextInput edit;std::string applied="unchanged";int commits=0;
    auto callback=[&](std::string s){applied=s;++commits;};
    edit.open("airtune","中文搜索","",80,font,callback);
    assert(std::filesystem::is_regular_file("/tmp/c1max-ime.pid"));
    for(char c:std::string("nihao"))assert(edit.key(c));
    assert(edit.candidates().at(0).text=="你好"&&applied=="unchanged");
    lv_refr_now(nullptr);edit.key(LV_KEY_ENTER);assert(edit.text()=="你好"&&edit.active()&&commits==0);
    edit.key(LV_KEY_BACKSPACE);assert(edit.text()=="你");
    edit.key('w');edit.key('s');assert(!edit.preedit().empty());
    edit.key(screen::KEY_EXIT);assert(edit.active()&&edit.preedit().empty());
    // Full repaint by an async app worker cannot steal or destroy the draft.
    lv_obj_clean(lv_screen_active());lv_refr_now(nullptr);assert(edit.text()=="你");
    edit.key(LV_KEY_ENTER);assert(applied=="你"&&commits==1&&!edit.active());
    assert(!std::filesystem::exists("/tmp/c1max-ime.pid"));
    edit.open("airtune","Cancel","Original",80,font,callback);edit.key('a');edit.key(screen::KEY_EXIT);edit.key(screen::KEY_EXIT);assert(applied=="你"&&commits==1);
    edit.open("airtune","ASCII + byte limits","",32,font,callback,false,3);
    for(char c:std::string("nihao"))edit.key(c);edit.key(' ');assert(edit.text()=="你好");edit.key(LV_KEY_ENTER);assert(edit.active()&&commits==1);
    edit.key(LV_KEY_BACKSPACE);edit.key(LV_KEY_ENTER);assert(commits==2&&applied=="你");
    edit.open("airtune","English","",80,font,callback);edit.key(screen::KEY_SYMBOL);for(char c:std::string("W/s:http"))edit.key(c);assert(edit.text()=="W/s:http");edit.key(LV_KEY_ENTER);assert(applied=="W/s:http"&&commits==3);
    edit.open("airtune","Power","",80,font,callback);assert(!edit.key(screen::KEY_HOME)&&!edit.active()&&commits==3);
    edit.open("no-such-app","Missing dictionary","",80,font,callback);edit.key('x');edit.key(LV_KEY_ENTER);assert(applied=="x"&&commits==4);
    edit.close();lv_tiny_ttf_destroy(font);screen::close();puts("PASS shared editor: real Pinyin, separate commit/confirm, draft cancellation, async repaint, UTF-8 deletion, byte limits, English, power and missing-data fallback");
}
