// Actual Settings widgets and persistence, with the offscreen LVGL backend.
#define main settings_app_main
#include "../src/main.cpp"
#undef main
#include <cassert>
#include <filesystem>

int main(int argc, char **argv) {
    assert(argc == 2);
    setenv("C1_APPS_DATA", argv[1], 1);
    std::filesystem::create_directories(argv[1]);
    load_voice_settings();
    assert(!voice.enabled && voice.use_moonpilot);
    voice.enabled = true;
    assert(save_voice_settings());
    voice = VoiceSettings{};
    load_voice_settings();
    assert(voice.enabled && voice.use_moonpilot && voice.endpoint.empty());
    voice.use_moonpilot = false;
    voice.endpoint = "http://localhost:8080/voice";
    voice.model = "中文-model";
    voice.token = "escaped-\"-\\-key";
    assert(save_voice_settings());
    voice = VoiceSettings{};
    load_voice_settings();
    assert(voice.model == "中文-model" && voice.token == "escaped-\"-\\-key");
    struct stat st{};
    assert(!stat(voice_settings_file().c_str(), &st) && (st.st_mode & 0777) == 0600);

    assert(screen::open());
    font = small = const_cast<lv_font_t *>(&lv_font_montserrat_18);
    section = static_cast<int>(Section::Voice);
    create_ui();
    lv_obj_update_layout(lv_screen_active());
    lv_area_t bounds{};
    lv_obj_get_coords(side_items[kSections - 1], &bounds);
    assert(bounds.y2 < 340);
    open_sheet(Sheet::Voice);
    assert(input_target == 4);
    const auto initial_endpoint = voice_endpoint_draft;
    move_focus(1);
    assert(input_target == 5);
    physical_key('X');
    assert(voice_model_draft == "中文-modelX");
    assert(voice_endpoint_draft == initial_endpoint);
    move_focus(1);
    assert(input_target == 6);
    physical_key('Y');
    assert(voice_token_draft == "escaped-\"-\\-keyY");
    move_focus(-1);
    physical_key(LV_KEY_BACKSPACE);
    assert(voice_model_draft == "中文-model");
    close_sheet();
    assert(voice_token_draft.empty());
    load_voice_settings();
    assert(voice.token == "escaped-\"-\\-key");  // cancel never persisted drafts
    lv_obj_clean(lv_screen_active());
    screen::close();
    puts("PASS Settings: MoonPilot/standalone persistence, private JSON, all 8 categories, field focus and cancel");
}
