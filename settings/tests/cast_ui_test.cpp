// Exercise real Settings choices and persistence without network/device I/O.
#define main settings_app_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <filesystem>
int main(int argc,char**argv) {
    assert(argc==2);setenv("C1_APPS_DATA",argv[1],1);
    std::filesystem::create_directories(argv[1]);
    assert(screen::open());font=small=const_cast<lv_font_t*>(&lv_font_montserrat_18);
    section=static_cast<int>(Section::Cast);create_ui();
    for(const std::string app:{"nes","pcsx4all","dosbox","streamplayer","bilibili","airtune"}) {
        auto choices=cast_page();auto it=std::find_if(choices.begin(),choices.end(),[&](const Item&i){return i.key=="cast-output-"+app;});
        assert(it!=choices.end()&&it->kind==Kind::Choice&&it->options.size()==3);
        for(int option=0;option<3;option++) {
            it->on_change(option);
            assert(static_cast<int>(casting::output_mode(app))==option);
        }
    }
    assert(casting::set_output_mode("nes",casting::OutputMode::Local));
    assert(casting::output_mode("bilibili")==casting::OutputMode::Both);
    lv_obj_clean(lv_screen_active());screen::close();
    puts("PASS Settings: per-app local/remote/both choices and independent persistence");
}
