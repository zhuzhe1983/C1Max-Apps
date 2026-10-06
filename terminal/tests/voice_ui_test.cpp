// Actual terminal key handling and font replacement with offscreen LVGL.
#define main terminal_app_main
#include "../src/main.cpp"
#undef main
#include <cassert>

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(screen::open());
    font_root = argv[1];
    const auto path = font_root + "/terminal/assets/JetBrainsMono-Regular.ttf";
    regular = font_file(path, 16, 160);
    small = font_file(path, 13, 96);
    assert(regular && small);
    terminal::Terminal term(14, 80); model = &term;
    terminal::Pty pty; shell_pty = &pty;
    assert(pty.start({"/bin/sh", "-c", "sleep 10"}, 14, 80, "/", {}));
    canvas = lv_obj_create(lv_screen_active());
    voice_preview_label = lv_label_create(lv_screen_active());
    lv_label_set_text(voice_preview_label, "Speech preview");
    lv_obj_set_style_text_font(voice_preview_label, regular, 0);
    resize_font(2);
    assert(font_size == 18);
    assert(lv_obj_get_style_text_font(voice_preview_label, LV_PART_MAIN) == regular);
    lv_refr_now(nullptr);  // renders after the old font was released
    voice_config.enabled = false;
    key(screen::KEY_SYMBOL); key('v');
    assert(term.take_output() == "\x16");  // optional voice must not steal Ctrl-V
    voice_config.enabled = true;
    key(screen::KEY_SYMBOL); key('v');
    assert(term.take_output().empty() && !voice_recording);  // missing config, no microphone
    key(screen::KEY_HOME);
    assert(screen::quit);
    screen::quit = false;
    key(screen::KEY_HOME_LONG);
    assert(screen::quit);
    assert(term.take_output().empty());
    pty.stop(); shell_pty = nullptr;
    lv_obj_clean(lv_screen_active());
    release_fonts(); screen::close();
    puts("PASS Terminal: live speech-preview font resize and short/long Power return to launcher");
}
