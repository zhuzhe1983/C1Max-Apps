#define main crosspoint_app_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <filesystem>
int main(int argc,char**argv){
    assert(argc==3);setenv("C1_APPS_ROOT",argv[1],1);setenv("C1_APPS_DATA",argv[2],1);
    std::filesystem::create_directories(data_dir());assert(screen::open());
    font=lv_tiny_ttf_create_file(("A:"+c1::root()+"/shared/NotoSansSC-Regular.ttf").c_str(),18);assert(font);
    for(int i=0;i<15;i++)books.push_back("Book "+std::to_string(i));library();
    key('s');assert(view==View::Library&&book_index==1);key('w');assert(book_index==0);
    key('e');assert(book_index==6);key('q');assert(book_index==0);key('g');assert(view==View::Settings);
    key('s');assert(std::string(lv_textarea_get_text(fields[0]))=="s");key(screen::KEY_EXIT);assert(view==View::Library);
    pages={"First page","Second page"};reader_text="First pageSecond page";reading_end=reader_text.size();page=0;show_page();key('e');assert(page==1);key('q');assert(page==0);
    feed.search="https://example.invalid/opds?query={searchTerms}";search();
    key('w');key('s');key('q');key('e');assert(std::string(lv_textarea_get_text(query_field))=="wsqe");
    for(int i=0;i<4;i++)key(LV_KEY_BACKSPACE);key(screen::KEY_SYMBOL);assert(ime&&ime->ready()&&ime->mode()==c1ime::Mode::Chinese);
    for(char c:std::string("nihao"))key(c);assert(ime->candidates().at(0).text=="你好");
    key(LV_KEY_ENTER);assert(view==View::Search&&job==Job::None);assert(std::string(lv_textarea_get_text(query_field))=="你好");
    assert(crosspoint::search_url(feed.search,lv_textarea_get_text(query_field))=="https://example.invalid/opds?query=%E4%BD%A0%E5%A5%BD");
    key('n');key('i');key(LV_KEY_BACKSPACE);assert(!ime->buffer().empty());key(screen::KEY_EXIT);assert(view==View::Search&&ime->state()==c1ime::State::Inactive);
    key(LV_KEY_BACKSPACE);assert(std::string(lv_textarea_get_text(query_field))=="你");
    // Build/repaint the real candidate bar with CJK text, then leave cleanly.
    for(char c:std::string("zhongwen"))key(c);lv_refr_now(nullptr);key(screen::KEY_EXIT);key(screen::KEY_EXIT);assert(view==View::Catalog&&!ime);
    lv_obj_clean(lv_screen_active());lv_obj_set_style_text_font(lv_screen_active(),LV_FONT_DEFAULT,0);lv_tiny_ttf_destroy(font);font=nullptr;screen::close();
    puts("PASS CrossPoint UI: W/S focus, G settings, Q/E pages, literal search input, real Rime commit/cancel/delete and UTF-8 OPDS query");
}
