#include "model.hpp"
#include "display.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {
volatile sig_atomic_t interrupted = 0;
void on_signal(int) { interrupted = 1; }
calculator::Model model;
lv_obj_t *result_label = nullptr, *history_label = nullptr, *message_label = nullptr;
lv_font_t *body_font = nullptr, *result_font = nullptr;
bool chinese = false;
const char *tr(const char *cn, const char *en) { return chinese ? cn : en; }

// Shared input translates the physical Shift combinations to ASCII digits and
// symbols. Unshifted letters are extra application shortcuts, not a hardware
// keymap. Consume one queue rather than also binding a LVGL keypad group.
bool physical_key(uint32_t code) {
    if (code == screen::KEY_HOME) { screen::quit = true; return false; }
    if (code == LV_KEY_ENTER || code == '\r' || code == '=') { model.evaluate(); return true; }
    if (code == LV_KEY_BACKSPACE) { model.backspace(); return true; }
    if (code >= 'A' && code <= 'Z') code += 'a' - 'A';
    if (code == ' ' || code == 'c') { model.clear(); return true; }
    if (code == 'x') { model.toggle_sign(); return true; }
    if (code > 127) return false;
    char ch = static_cast<char>(code);
    static constexpr char letters[] = "qwertyuiop";
    const char *number = std::strchr(letters, ch);
    if (ch && number) ch = "1234567890"[number - letters];
    else {
        switch (ch) {
        case 'a': ch = '+'; break;
        case 's': ch = '-'; break;
        case 'd': ch = '*'; break;
        case 'f': ch = '/'; break;
        case 'z': ch = '.'; break;
        case 'j': ch = '('; break;
        case 'k': ch = ')'; break;
        case 'l': ch = '^'; break;
        default:
            if (!((ch >= '0' && ch <= '9') || std::strchr(".+-*/^()", ch))) return false;
            break;
        }
    }
    model.input(ch);
    return true;
}

lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y, int width,
                uint32_t color, const lv_font_t *font = nullptr) {
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    if (font) lv_obj_set_style_text_font(obj, font, 0);
    return obj;
}

void refresh() {
    lv_label_set_text(result_label, model.display().c_str());
    lv_label_set_text(history_label, model.history().c_str());
    const auto &error = model.error();
    const char *message = tr("按通常的运算优先级计算", "Standard operator precedence");
    if (error == "division_by_zero") message = tr("不能除以零；可退格修改", "Cannot divide by zero; use backspace");
    else if (error == "too_long") message = tr("表达式过长；请退格或清空", "Expression too long; delete or clear");
    else if (error == "range") message = tr("结果超出范围或无实数结果", "Out of range or no real result");
    else if (!error.empty()) message = tr("表达式不完整；请检查括号", "Incomplete expression; check brackets");
    else if (model.evaluated()) message = tr("输入数字重算，按运算符继续", "Digits start over; operators continue");
    lv_label_set_text(message_label, message);
    lv_obj_set_style_text_color(message_label, lv_color_hex(error.empty() ? 0x96a9bf : 0xffb7a5), 0);
    lv_obj_set_style_text_color(result_label, lv_color_hex(error.empty() ? 0xf4f8fc : 0xffb7a5), 0);
}

void key(lv_event_t *event) {
    const auto *action = static_cast<const char *>(lv_event_get_user_data(event));
    if (!std::strcmp(action, "clear")) model.clear();
    else if (!std::strcmp(action, "back")) model.backspace();
    else if (!std::strcmp(action, "sign")) model.toggle_sign();
    else if (!std::strcmp(action, "=")) model.evaluate();
    else model.input(action[0]);
    refresh();
}

void button(lv_obj_t *parent, const char *text, const char *action,
            int x, int y, int width, int height, uint32_t color) {
    auto *obj = lv_button_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x507397), LV_STATE_PRESSED);
    lv_obj_set_style_text_color(obj, lv_color_hex(0xf4f8fc), 0);
    if (body_font) lv_obj_set_style_text_font(obj, body_font, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    auto *text_obj = lv_label_create(obj);
    lv_label_set_text(text_obj, text);
    lv_obj_set_style_text_color(text_obj, lv_color_hex(0xf4f8fc), 0);
    lv_obj_center(text_obj);
    lv_obj_add_event_cb(obj, key, LV_EVENT_CLICKED, const_cast<char *>(action));
}

void create_ui() {
    auto *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(0x111b2a), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(root, lv_color_hex(0xf4f8fc), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    if (body_font) lv_obj_set_style_text_font(root, body_font, 0);
    label(root, tr("计算器", "Calculator"), 18, 12, 116, 0xf4f8fc);
    label(root, tr("Shift+Q…P 数字 · A + · Enter =", "Shift+Q..P digits / A + / Enter ="), 144, 14, 528, 0x97aec7);

    // The left panel stays clear of the large touchscreen keypad.
    history_label = label(root, "", 20, 72, 298, 0x97aec7);
    lv_label_set_long_mode(history_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_anim_duration(history_label, 7000, 0);
    result_label = label(root, "0", 20, 118, 298, 0xf4f8fc, result_font);
    lv_label_set_long_mode(result_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(result_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_anim_duration(result_label, 7000, 0);
    message_label = label(root, "", 20, 180, 298, 0x97aec7);
    lv_label_set_long_mode(message_label, LV_LABEL_LONG_WRAP);
    label(root, "Shift: J*  C/  X-  Z.", 20, 238, 298, 0x97aec7);
    button(root, "(", "(", 20, 266, 90, 56, 0x26394f);
    button(root, ")", ")", 124, 266, 90, 56, 0x26394f);
    button(root, "x ^ y", "^", 228, 266, 90, 56, 0x26394f);

    struct Key { const char *text; const char *action; };
    static const Key keys[4][4] = {
        {{"AC","clear"}, {"+/-","sign"}, {"DEL","back"}, {"/","/"}},
        {{"7","7"}, {"8","8"}, {"9","9"}, {"*","*"}},
        {{"4","4"}, {"5","5"}, {"6","6"}, {"-","-"}},
        {{"1","1"}, {"2","2"}, {"3","3"}, {"+","+"}}
    };
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const auto &k = keys[row][col];
            const uint32_t color = col == 3 ? 0x225973 : row == 0 ? 0x3b4d65 : 0x273a52;
            button(root, k.text, k.action, 340 + 113 * col, 58 + 56 * row, 105, 49, color);
        }
    }
    button(root, "0", "0", 340, 282, 105, 49, 0x273a52);
    button(root, ".", ".", 453, 282, 105, 49, 0x273a52);
    button(root, "=", "=", 566, 282, 218, 49, 0x227b75);
    refresh();
}

bool parse_duration(const char *text, unsigned long &value) {
    if (!text || !*text || *text == '-') return false;
    char *end = nullptr;
    errno = 0;
    value = std::strtoul(text, &end, 10);
    return !errno && !*end && value > 0 && value <= 3600000;
}
}

int main(int argc, char **argv) {
    // This path exercises the exact device arithmetic without opening the display.
    if (argc == 3 && std::strcmp(argv[1], "--evaluate") == 0) {
        const auto result = calculator::evaluate_expression(argv[2]);
        if (!result.ok) { std::fprintf(stderr, "%s\n", result.error_message.c_str()); return 2; }
        std::puts(calculator::format_value(result.value).c_str());
        return 0;
    }
    unsigned long smoke_ms = 0;
    if (argc != 1 && !(argc == 3 && !std::strcmp(argv[1], "--smoke-ms") && parse_duration(argv[2], smoke_ms))) {
        std::fprintf(stderr, "Usage: %s [--evaluate EXPRESSION | --smoke-ms 1..3600000]\n", argv[0]);
        return 2;
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    if (!screen::open()) { screen::close(); return 1; }
    const char *apps_root = std::getenv("C1_APPS_ROOT");
    const std::string font_path = "A:" + std::string(apps_root ? apps_root : "/storage/apps/current") + "/shared/NotoSansSC-Regular.ttf";
    body_font = lv_tiny_ttf_create_file(font_path.c_str(), 22);
    result_font = lv_tiny_ttf_create_file(font_path.c_str(), 38);
    chinese = body_font != nullptr;
    if (!body_font) std::fprintf(stderr, "Calculator font unavailable: %s; using English UI\n", font_path.c_str());
    create_ui();
    const auto started = screen::tick();
    while (!interrupted && !screen::quit && (!smoke_ms || screen::tick() - started < smoke_ms)) {
        const auto wait_ms = std::min<uint32_t>(lv_timer_handler(), 20);
        bool changed = false;
        for (uint32_t code; (code = screen::take_key()) != 0;) changed = physical_key(code) || changed;
        if (changed) refresh();
        usleep(std::max<uint32_t>(wait_ms, 1) * 1000);
    }
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_text_font(lv_screen_active(), LV_FONT_DEFAULT, 0);
    if (result_font) lv_tiny_ttf_destroy(result_font);
    if (body_font) lv_tiny_ttf_destroy(body_font);
    screen::close();
    return 0;
}
