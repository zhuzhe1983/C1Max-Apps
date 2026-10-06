#include "terminal.hpp"
#include "pty.hpp"
#include "input.hpp"
#include "display.hpp"
#include "c1ime.hpp"
#include "net.hpp"
#include "voice.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <future>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
int rows = 14, cols = 80, cell_width = 10, cell_height = 22,font_size=16;
constexpr int body_height = 308;
terminal::Pty *shell_pty=nullptr;
std::string font_root;
uint32_t last_resize=0;
void resize_font(int delta);
struct FontShortcuts {
    FontShortcuts(){int fd=open("/tmp/c1max-terminal-font.pid",O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);if(fd>=0){dprintf(fd,"%ld\n",(long)getpid());close(fd);}}
    ~FontShortcuts(){unlink("/tmp/c1max-terminal-font.pid");}
};
volatile sig_atomic_t interrupted = 0;
void signal_handler(int) { interrupted = 1; }
lv_font_t *regular = nullptr, *bold = nullptr, *cjk = nullptr, *small = nullptr;
lv_obj_t *voice_button = nullptr, *voice_button_label = nullptr, *voice_preview_label = nullptr;
lv_obj_t *canvas = nullptr, *status = nullptr;
terminal::Terminal *model = nullptr;
terminal::Input input;
std::unique_ptr<c1ime::Engine> ime;
std::string ime_error;
std::string persistent_error;
bool caps = false;
enum class TextMode { Lower, Upper, Chinese };
TextMode text_mode = TextMode::Lower;
std::string last_status;
bool pending_ctrl_a = false;
uint32_t pending_ctrl_a_at = 0;
terminal::VoiceConfig voice_config;
terminal::VoiceRecorder voice_recorder;
struct VoiceResult { std::string text, warning; };
std::future<VoiceResult> voice_job;
std::future<std::string> voice_preview_job;
bool voice_recording = false;
bool voice_finishing = false;
bool voice_busy = false;
bool voice_preview_busy = false, voice_preview_failed = false, voice_final_pending = false;
std::string voice_file;
std::string voice_final_wav, voice_context, voice_preview_text;
std::string last_voice_preview;
int last_voice_button_state = -1;
bool voice_preview_visible = false;
int voice_context_rows = 14, voice_context_cols = 80;
uint32_t voice_started_at = 0, voice_preview_at = 0;
std::atomic<bool> voice_cancelled{false};
std::atomic<bool> voice_preview_cancelled{false};
int touch_scroll_remainder = 0;
int touch_scroll_last_x = 0;
int touch_scroll_last_y = 0;
bool touch_scroll_have_point = false;

std::string environment(const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value && *value ? value : fallback;
}
bool readable(const std::string &path) { return access(path.c_str(), R_OK) == 0; }
void rectangle(lv_layer_t *layer, lv_area_t area, uint32_t color) {
    lv_draw_rect_dsc_t dsc; lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color); dsc.bg_opa = LV_OPA_COVER;
    dsc.border_width = 0; dsc.radius = 0;
    lv_draw_rect(layer, &dsc, &area);
}
void draw(lv_event_t *event) {
    if (!model) return;
    auto *layer = lv_event_get_layer(event);
    const auto original_clip = layer->_clip_area;
    auto cursor = model->cursor();
    for (int row = 0; row < rows; ++row) {
        if (row * cell_height > original_clip.y2 || (row + 1) * cell_height <= original_clip.y1) continue;
        for (int col = 0; col < cols; ++col) {
            auto cell = model->cell(row, col);
            if (cell.chars[0] == UINT32_MAX) continue; // Already drawn by its double-width base cell.
            int width = cell.width == 2 && col + 1 < cols ? 2 : 1;
            lv_area_t area{col * cell_width, row * cell_height,
                           (col + width) * cell_width - 1, (row + 1) * cell_height - 1};
            lv_area_t clipped{std::max(area.x1, original_clip.x1), std::max(area.y1, original_clip.y1),
                              std::min(area.x2, original_clip.x2), std::min(area.y2, original_clip.y2)};
            if (clipped.x1 > clipped.x2 || clipped.y1 > clipped.y2) continue;
            layer->_clip_area = clipped;
            auto fg = model->rgb(cell.fg), bg = model->rgb(cell.bg);
            if (cell.attrs.reverse) std::swap(fg, bg);
            bool selected = model->cursor_visible() && cursor.row == row && cursor.col == col;
            if (selected) { bg = 0x8fd7cf; fg = 0x0c121c; }
            rectangle(layer, area, bg);
            if (!cell.attrs.conceal && cell.chars[0]) {
                auto text = terminal::Terminal::utf8(cell);
                auto *font = cell.attrs.bold && bold ? bold : regular;
                lv_draw_label_dsc_t label; lv_draw_label_dsc_init(&label);
                label.font = font; label.color = lv_color_hex(fg);
                label.text = text.c_str(); label.text_local = 1;
                label.flag = LV_TEXT_FLAG_EXPAND; label.bidi_dir = LV_BASE_DIR_LTR;
                auto text_area = area;
                text_area.y1 += std::max(0, (cell_height - int(font->line_height)) / 2);
                lv_draw_label(layer, &label, &text_area);
            }
            if (cell.attrs.underline) {
                auto line = area; line.y1 = line.y2 - 1; rectangle(layer, line, fg);
            }
            if (cell.attrs.strike) {
                auto line = area; line.y1 = line.y2 = (area.y1 + area.y2) / 2; rectangle(layer, line, fg);
            }
        }
    }
    layer->_clip_area = original_clip;
}
void set_status(const std::string &shell_state = {}) {
    std::string text;
    if (!persistent_error.empty()) text = persistent_error + "  |  Power: home";
    else if (voice_recording) text = "VOICE 录音 " + std::to_string((screen::tick() - voice_started_at) / 1000) +
        "s · 点击结束" + (voice_preview_failed ? " · 实时预览暂不可用" : "");
    else if (voice_finishing) text = "VOICE 正在准备录音…";
    else if (voice_busy) text = "VOICE 正在识别和润色…";
    else if (!shell_state.empty()) text = shell_state + "  |  Power: home";
    else {
        if (ime && ime->ready() && ime->mode() == c1ime::Mode::Chinese) {
            text = "拼 ";
            const auto buffer = ime->buffer();
            if (!buffer.empty()) text += buffer + "  ";
            const auto candidates = ime->candidates();
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (i) text += "  ";
                text += std::to_string(i + 1) + "." + candidates[i].text;
            }
            if (ime->state() == c1ime::State::Inactive) text += "  双击Shift: abc";
        } else {
            text = caps ? "CAPS  " : "abc   ";
            text += input.hint();
            if (ime_error.empty() && ime && ime->ready()) text += "  双击Shift: 拼音";
            if (!ime_error.empty()) text += "  IME off: " + ime_error;
        }
        if (model->history_offset()) text = "HISTORY -" + std::to_string(model->history_offset()) + "  " + text;
    }
    if (text != last_status) { lv_label_set_text(status, text.c_str()); last_status = std::move(text); }
    if (voice_button) {
        const bool active = voice_recording || voice_finishing || voice_busy || voice_final_pending || voice_preview_busy;
        const int state = voice_recording ? 1 : active ? 2 : 0;
        if (state != last_voice_button_state) {
            last_voice_button_state = state;
            lv_label_set_text(voice_button_label, state == 1 ? "结束" : state == 2 ? "处理" : "语音");
            lv_obj_set_style_bg_color(voice_button, lv_color_hex(state == 1 ? 0x973c40 : 0x205d67), 0);
            if (state == 2) lv_obj_add_state(voice_button, LV_STATE_DISABLED);
            else lv_obj_remove_state(voice_button, LV_STATE_DISABLED);
        }
        if (active != voice_preview_visible) {
            voice_preview_visible = active;
            if (active) lv_obj_remove_flag(voice_preview_label, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(voice_preview_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (active) {
            const auto preview = voice_preview_text.empty() ?
                (voice_recording ? std::string("请说话，识别文字会显示在这里…") : std::string("正在识别和润色…")) : voice_preview_text;
            if (preview != last_voice_preview) {
                last_voice_preview = preview;
                lv_label_set_text(voice_preview_label, preview.c_str());
            }
        }
    }
}

void set_text_mode(TextMode next) {
    if (next == TextMode::Chinese) {
        if (ime && ime->ready() && ime->mode() != c1ime::Mode::Chinese) ime->toggle_mode();
    } else if (ime && ime->ready() && ime->mode() == c1ime::Mode::Chinese) {
        ime->toggle_mode();
    }
    text_mode = next;
    caps = next == TextMode::Upper;
}

void cycle_text_mode() {
    if (!ime || !ime->ready()) {
        caps = screen::caps_lock();
        text_mode = caps ? TextMode::Upper : TextMode::Lower;
        return;
    }
    switch (text_mode) {
    case TextMode::Lower: set_text_mode(TextMode::Upper); break;
    case TextMode::Upper: set_text_mode(TextMode::Chinese); break;
    case TextMode::Chinese: set_text_mode(TextMode::Lower); break;
    }
}

void toggle_ime_from_ctrl_a() {
    if (!ime || !ime->ready()) return;
    if (ime->mode() == c1ime::Mode::Chinese) {
        set_text_mode(screen::caps_lock() ? TextMode::Upper : TextMode::Lower);
    } else {
        set_text_mode(TextMode::Chinese);
    }
    ime_error.clear();
}

void send_terminal_text(const std::string &text) {
    if (text.empty() || !shell_pty) return;
    if (!shell_pty->send(text) && !shell_pty->error().empty()) persistent_error = shell_pty->error();
}

void start_final_voice_request() {
    if (!voice_final_pending || voice_preview_busy) return;
    voice_final_pending = false;
    voice_finishing = false;
    const auto config = voice_config;
    auto wav = std::move(voice_final_wav);
    const auto context = voice_context;
    const int context_rows = voice_context_rows, context_cols = voice_context_cols;
    voice_busy = true;
    voice_cancelled = false;
    persistent_error.clear();
    voice_job = std::async(std::launch::async, [config, wav = std::move(wav), context, context_rows, context_cols] {
        VoiceResult result;
        result.text = terminal::request_voice(config, wav, context, context_rows, context_cols, &voice_cancelled, false, &result.warning);
        return result;
    });
}

void finish_voice_recording() {
    voice_recording = false;
    voice_preview_cancelled = true;
    if (!voice_recorder.error().empty()) {
        voice_finishing = false;
        unlink(voice_file.c_str());
        persistent_error = voice_recorder.error();
        return;
    }
    try {
        voice_final_wav = c1::read_file(voice_file, 700000);
    } catch (const std::exception &e) {
        voice_finishing = false;
        persistent_error = std::string("读取录音失败：") + e.what();
        unlink(voice_file.c_str());
        return;
    }
    unlink(voice_file.c_str());
    voice_final_pending = true;
    start_final_voice_request();
}

void toggle_voice_recording() {
    if (voice_busy || voice_finishing || (!voice_recording && voice_preview_busy)) return;
    if (voice_recording) {
        voice_finishing = true;
        voice_preview_cancelled = true;
        voice_recorder.finish();
        return;
    }
    if (!voice_config.enabled) {
        persistent_error = "语音未配置，请到设置 → 语音输入填写服务";
        return;
    }
    if (voice_config.endpoint.empty() || voice_config.model.empty()) {
        persistent_error = voice_config.use_moonpilot ? "请先在 MoonPilot 设置中配置 ASR 服务" : "请到设置 → 语音输入填写服务";
        return;
    }
    if (!shell_pty || !shell_pty->running()) {
        persistent_error = "Shell 已退出，不能开始语音输入";
        return;
    }
    if (!voice_recorder.start(voice_file)) {
        persistent_error = voice_recorder.error();
        return;
    }
    if (ime && ime->ready() && ime->state() != c1ime::State::Inactive) ime->cancel();
    persistent_error.clear();
    voice_recording = true;
    voice_finishing = false;
    voice_preview_failed = false;
    voice_preview_text.clear();
    voice_preview_cancelled = false;
    voice_context = model ? model->context() : std::string();
    voice_context_rows = model ? model->rows() : rows;
    voice_context_cols = model ? model->cols() : cols;
    voice_started_at = voice_preview_at = screen::tick();
}

void poll_voice_preview() {
    if (voice_preview_busy && voice_preview_job.valid() &&
        voice_preview_job.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        try {
            const auto text = voice_preview_job.get();
            if (!text.empty() && voice_recording && !voice_finishing) voice_preview_text = text;
        } catch (...) {
            if (!voice_preview_cancelled) voice_preview_failed = true;
        }
        voice_preview_busy = false;
        start_final_voice_request();
    }
    if (!voice_config.live_preview || voice_preview_failed || voice_preview_busy ||
        !voice_recording || voice_finishing || uint32_t(screen::tick() - voice_preview_at) < 1200) return;
    voice_preview_at = screen::tick();
    try {
        auto wav = voice_recorder.snapshot();
        if (wav.empty()) return;
        const auto config = voice_config;
        const auto context = voice_context;
        const int context_rows = voice_context_rows, context_cols = voice_context_cols;
        voice_preview_busy = true;
        voice_preview_job = std::async(std::launch::async, [config, wav = std::move(wav), context, context_rows, context_cols] {
            return terminal::request_voice(config, wav, context, context_rows, context_cols, &voice_preview_cancelled, true);
        });
    } catch (...) {
        // The recorder may not have flushed its first WAV block yet.
        voice_preview_busy = false;
    }
}

void voice_click(lv_event_t *) { toggle_voice_recording(); }

void touch_scroll(lv_event_t *event) {
    if (!model) return;
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        touch_scroll_remainder = 0;
        touch_scroll_have_point = false;
        return;
    }
    if (code != LV_EVENT_PRESSING) return;
    auto *indev = lv_indev_active();
    if (!indev) return;
    lv_point_t point{};
    lv_indev_get_point(indev, &point);
    if (!touch_scroll_have_point) {
        touch_scroll_last_x = point.x;
        touch_scroll_last_y = point.y;
        touch_scroll_have_point = true;
        return;
    }
    const int dx = point.x - touch_scroll_last_x;
    const int dy = point.y - touch_scroll_last_y;
    touch_scroll_last_x = point.x;
    touch_scroll_last_y = point.y;
    if (dy == 0 || std::abs(dy) < std::abs(dx)) return;
    // A terminal behaves like a document while dragging: the visible
    // content follows the finger.  Dragging down reveals older scrollback;
    // dragging up returns toward the live prompt.
    touch_scroll_remainder += dy;
    const int lines = touch_scroll_remainder / cell_height;
    if (!lines) return;
    touch_scroll_remainder -= lines * cell_height;
    model->scroll_history(lines);
    lv_obj_invalidate(canvas);
}

void route_terminal_output(const std::string &output) {
    if (output.empty() || !shell_pty) return;
    if (pending_ctrl_a) {
        if (output == " " && ime && ime->ready()) {
            pending_ctrl_a = false;
            toggle_ime_from_ctrl_a();
            return;
        }
        send_terminal_text("\x01");
        pending_ctrl_a = false;
    }
    if (output == "\x01") {
        pending_ctrl_a = true;
        pending_ctrl_a_at = screen::tick();
        return;
    }
    send_terminal_text(output);
}

bool ime_key(uint32_t code) {
    if (!ime || !ime->ready() || ime->mode() != c1ime::Mode::Chinese) return false;
    const auto state = ime->state();
    if (pending_ctrl_a && code == ' ') {
        pending_ctrl_a = false;
        toggle_ime_from_ctrl_a();
        return true;
    }
    if (state != c1ime::State::Inactive && input.mode() == terminal::Input::Mode::Navigation) {
        const auto lower = code >= 'A' && code <= 'Z' ? code + ('a' - 'A') : code;
        if (lower == 'w' || lower == 'a' || lower == 'z' ||
            lower == 's' || lower == 'd' || lower == 'x') {
            input.escape(*model);  // consume the one-shot navigation prefix
            if (lower == 'w' || lower == 'a' || lower == 'z') ime->page_up();
            else ime->page_down();
            return true;
        }
    }
    if (input.mode() != terminal::Input::Mode::Text) return false;
    if (state != c1ime::State::Inactive) {
        if (code >= '1' && code <= '9') {
            send_terminal_text(ime->select(static_cast<int>(code - '1')));
            return true;
        }
        if (code == ' ') {
            send_terminal_text(ime->select(0));
            return true;
        }
        if (code == LV_KEY_ENTER || code == '\r') {
            if (state == c1ime::State::Selecting) send_terminal_text(ime->select(0));
            ime->cancel();
            return true;
        }
        if (code == LV_KEY_BACKSPACE) { ime->backspace(); return true; }
        if (code == screen::KEY_EXIT || code == LV_KEY_ESC) { ime->cancel(); return true; }
        if (code == LV_KEY_UP || code == LV_KEY_LEFT) { ime->page_up(); return true; }
        if (code == LV_KEY_DOWN || code == LV_KEY_RIGHT || code == LV_KEY_NEXT) { ime->page_down(); return true; }
    }
    if (code >= 32 && code < 127 && input.mode() == terminal::Input::Mode::Text) {
        char ch = static_cast<char>(code);
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
        if (ime->input(ch)) {
            send_terminal_text(ime->take_commit());
            return true;
        }
    }
    return false;
}

void key(uint32_t code) {
    if(code==screen::KEY_FONT_UP||code==screen::KEY_FONT_DOWN){resize_font(code==screen::KEY_FONT_UP?2:-2);return;}
    if (code == screen::KEY_HOME || code == screen::KEY_HOME_LONG) { screen::quit = true; return; }
    if (code == screen::KEY_MODE) { cycle_text_mode(); return; }
    if (voice_config.enabled && input.mode() == terminal::Input::Mode::Control && (code == 'v' || code == 'V')) {
        input.escape(*model);
        toggle_voice_recording();
        return;
    }
    if (code == screen::KEY_SYMBOL) { input.symbol(); return; }
    if (pending_ctrl_a && code != ' ') { send_terminal_text("\x01"); pending_ctrl_a = false; }
    if (ime_key(code)) return;
    if (code == screen::KEY_EXIT || code == LV_KEY_ESC) { input.escape(*model); return; }
    switch (code) {
    case LV_KEY_ENTER: case '\r': input.key(*model, VTERM_KEY_ENTER); break;
    case LV_KEY_BACKSPACE: input.key(*model, VTERM_KEY_BACKSPACE); break;
    case LV_KEY_UP: input.key(*model, VTERM_KEY_UP); break;
    case LV_KEY_DOWN: input.key(*model, VTERM_KEY_DOWN); break;
    case LV_KEY_LEFT: input.key(*model, VTERM_KEY_LEFT); break;
    case LV_KEY_RIGHT: input.key(*model, VTERM_KEY_RIGHT); break;
    case LV_KEY_HOME: input.key(*model, VTERM_KEY_HOME); break;
    case LV_KEY_END: input.key(*model, VTERM_KEY_END); break;
    case LV_KEY_DEL: input.key(*model, VTERM_KEY_DEL); break;
    case LV_KEY_NEXT: input.key(*model, VTERM_KEY_TAB); break;
    default:
        if (code >= 32 && code < 0x10000) {
            // The shared keymap still toggles its physical caps bit on every
            // double-Shift. In the three-state terminal cycle, Chinese→abc
            // and abc→CAPS can be on opposite physical caps states; normalize
            // ordinary text to the selected terminal state here.
            if (text_mode == TextMode::Lower && code >= 'A' && code <= 'Z') code += 'a' - 'A';
            if (text_mode == TextMode::Upper && code >= 'a' && code <= 'z') code -= 'a' - 'A';
            input.character(*model, code);
        }
        break;
    }
}
lv_font_t *font_file(const std::string &path, int size, size_t cache) {
    return lv_tiny_ttf_create_file_ex(("A:" + path).c_str(), size, LV_FONT_KERNING_NONE, cache);
}
void resize_font(int delta){
    auto now=screen::tick();if(last_resize&&uint32_t(now-last_resize)<180)return;
    int size=std::clamp(font_size+delta,12,28);if(size==font_size||!model||!shell_pty)return;
    auto *next=font_file(font_root+"/terminal/assets/JetBrainsMono-Regular.ttf",size,160);
    auto *next_bold=font_file(font_root+"/terminal/assets/JetBrainsMono-Bold.ttf",size,96);
    auto *next_cjk=font_file(font_root+"/shared/NotoSansSC-Regular.ttf",size,96);
    if(!next){if(next_bold)lv_tiny_ttf_destroy(next_bold);if(next_cjk)lv_tiny_ttf_destroy(next_cjk);return;}
    int cw=(size*3+4)/5,ch=size+6,new_rows=body_height/ch,new_cols=800/cw;
    if(!shell_pty->resize(new_rows,new_cols)){for(auto*f:{next,next_bold,next_cjk})if(f)lv_tiny_ttf_destroy(f);return;}
    if(voice_preview_label)lv_obj_set_style_text_font(voice_preview_label,next,0);
    if(regular)regular->fallback=nullptr;if(bold)bold->fallback=nullptr;if(small)small->fallback=nullptr;
    for(auto*f:{regular,bold,cjk})if(f)lv_tiny_ttf_destroy(f);
    regular=next;bold=next_bold;cjk=next_cjk;regular->fallback=cjk;if(bold)bold->fallback=cjk;if(small)small->fallback=cjk;
    font_size=size;cell_width=cw;cell_height=ch;rows=new_rows;cols=new_cols;
    model->resize(rows,cols);last_resize=now;lv_obj_invalidate(canvas);
    std::fprintf(stderr,"[terminal] font=%d grid=%dx%d\n",font_size,cols,rows);
}
void release_fonts() {
    if (regular) regular->fallback = nullptr;
    if (bold) bold->fallback = nullptr;
    if (small) small->fallback = nullptr;
    for (auto *font : {regular, bold, cjk, small}) if (font) lv_tiny_ttf_destroy(font);
    regular = bold = cjk = small = nullptr;
}
}

int main() {
    signal(SIGTERM, signal_handler); signal(SIGINT, signal_handler); signal(SIGHUP, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    if (!screen::open()) { screen::close(); return 1; }
    int result = 0;
    try {
        const auto root = environment("C1_APPS_ROOT", "/storage/apps/current");
        const auto data = environment("C1_APPS_DATA", "/storage/apps/data");
        const auto assets = root + "/terminal/assets/";font_root=root;FontShortcuts font_shortcuts;
        const auto terminal_data = data + "/terminal";
        mkdir(data.c_str(), 0755); mkdir(terminal_data.c_str(), 0700);
        voice_file = terminal_data + "/voice.wav";
        std::string voice_config_error;
        const auto voice_config_path = terminal_data + "/voice.json";
        const bool has_voice_config = access(voice_config_path.c_str(), R_OK) == 0;
        if (has_voice_config && !terminal::load_voice_config(voice_config_path, voice_config, voice_config_error)) {
            persistent_error = "语音设置无效：" + voice_config_error;
        } else if (!has_voice_config) {
            // A fresh install follows MoonPilot without copying its secret
            // into a second settings file.
            voice_config.use_moonpilot = true;
        }
        if (voice_config.use_moonpilot) {
            std::string moonpilot_error;
            if (!terminal::load_moonpilot_config(data + "/moonpilot/settings.json", voice_config, moonpilot_error) &&
                voice_config.enabled)
                persistent_error = "MoonPilot 语音服务未配置：" + moonpilot_error;
        }
        regular = font_file(assets + "JetBrainsMono-Regular.ttf", 16, 160);
        bold = font_file(assets + "JetBrainsMono-Bold.ttf", 16, 96);
        small = font_file(assets + "JetBrainsMono-Regular.ttf", 13, 96);
        cjk = font_file(root + "/shared/NotoSansSC-Regular.ttf", 16, 96);
        if (!regular || !small) throw std::runtime_error("Terminal JetBrains Mono font is missing");
        if (cjk) { regular->fallback = cjk; if (bold) bold->fallback = cjk; small->fallback = cjk; }
        else persistent_error = "CJK font missing; Latin terminal still available";
        terminal::Terminal term(rows, cols); model = &term;
        terminal::Pty pty;shell_pty=&pty;
        auto *screen_root = lv_screen_active();
        lv_obj_remove_flag(screen_root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(screen_root, lv_color_hex(0x0c121c), 0);
        lv_obj_set_style_bg_opa(screen_root, LV_OPA_COVER, 0);
        canvas = lv_obj_create(screen_root); lv_obj_remove_style_all(canvas);
        lv_obj_set_pos(canvas, 0, 0); lv_obj_set_size(canvas, 800, body_height);
        lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(canvas, lv_color_hex(0x0c121c), 0);
        lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(canvas, draw, LV_EVENT_DRAW_MAIN, nullptr);
        lv_obj_add_event_cb(canvas, touch_scroll, LV_EVENT_ALL, nullptr);
        status = lv_label_create(screen_root);
        lv_obj_set_pos(status, 0, body_height); lv_obj_set_size(status, 724, 32);
        lv_obj_set_style_bg_opa(status, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(status, lv_color_hex(0x1d2b3d), 0);
        lv_obj_set_style_text_color(status, lv_color_hex(0xd7e2ed), 0);
        lv_obj_set_style_text_font(status, small, 0);
        lv_obj_set_style_pad_left(status, 4, 0); lv_obj_set_style_pad_top(status, 6, 0);
        lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
        voice_button = lv_button_create(screen_root);
        lv_obj_set_pos(voice_button, 724, body_height); lv_obj_set_size(voice_button, 76, 32);
        lv_obj_set_style_radius(voice_button, 0, 0);
        lv_obj_set_style_pad_all(voice_button, 0, 0);
        lv_obj_set_style_text_font(voice_button, small, 0);
        lv_obj_set_style_text_color(voice_button, lv_color_hex(0xf2f8f7), 0);
        voice_button_label = lv_label_create(voice_button); lv_obj_center(voice_button_label);
        lv_obj_add_event_cb(voice_button, voice_click, LV_EVENT_CLICKED, nullptr);
        voice_preview_label = lv_label_create(screen_root);
        lv_obj_set_pos(voice_preview_label, 8, body_height - 80); lv_obj_set_size(voice_preview_label, 784, 72);
        lv_obj_set_style_bg_opa(voice_preview_label, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(voice_preview_label, lv_color_hex(0x203b47), 0);
        lv_obj_set_style_text_color(voice_preview_label, lv_color_hex(0xf2f8f7), 0);
        lv_obj_set_style_text_font(voice_preview_label, regular, 0);
        lv_obj_set_style_pad_all(voice_preview_label, 8, 0);
        lv_label_set_long_mode(voice_preview_label, LV_LABEL_LONG_WRAP);
        lv_obj_add_flag(voice_preview_label, LV_OBJ_FLAG_HIDDEN);
        caps = screen::caps_lock();
        text_mode = caps ? TextMode::Upper : TextMode::Lower;
        std::string shell = environment("C1_TERMINAL_SHELL", "");
        if (shell.empty()) {
            for (const auto &candidate : {root + "/linux-tools/bin/bash", root + "/tools/bin/bash", std::string("/bin/bash"), std::string("/bin/sh")})
                if (access(candidate.c_str(), X_OK) == 0) { shell = candidate; break; }
        }
        const auto home = terminal_data;
        ime = std::make_unique<c1ime::Engine>(assets + "rime-data", home + "/rime");
        if (!ime->initialize()) ime_error = ime->error();
        auto path = root + "/terminal/assets/bin:" + root + "/linux-tools/bin:" + root + "/tools/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
        std::string terminfo = root + "/linux-tools/share/terminfo", termname = "xterm-256color";
        if (!readable(terminfo + "/x/xterm-256color") && !readable(terminfo + "/78/xterm-256color")) {
            terminfo = assets + "terminfo"; termname = "c1max";
        }
        std::vector<std::string> env = {"TERM=" + termname, "TERMINFO=" + terminfo,
            "COLORTERM=truecolor", "PATH=" + path, "HOME=" + home, "SHELL=" + shell,
            "ENV=" + assets + "shellrc", "LC_ALL=C", "LANG=C", "PS1=\\w # ",
            "HISTFILE=" + home + "/history", "INPUTRC=" + assets + "inputrc",
            "WGETRC=" + assets + "wgetrc",
            "SSL_CERT_FILE=" + root + "/shared/ca-certificates.crt",
            "CURL_CA_BUNDLE=" + root + "/shared/ca-certificates.crt"};
        // C.UTF-8 is not assumed to exist in the stock Buildroot glibc locale
        // archive. Output is always decoded as UTF-8; shell editing is locale-dependent.
        std::vector<std::string> argv{shell};
        if (shell.size() >= 4 && shell.compare(shell.size() - 4, 4, "bash") == 0) {
            argv.push_back("--noprofile"); argv.push_back("--rcfile"); argv.push_back(assets + "shellrc");
        }
        argv.push_back("-i");
        term.feed("\x1b[36mC1Max Terminal\x1b[0m  |  Symbol + C: interrupt  |  Power: home\r\n");
        term.feed("Commands: help exit ssh scp sshd (configure authentication first) vi/vim nano less\r\n");
        term.feed("Tools: grep sed awk find tar gzip unzip wget curl sqlite3 ps top\r\n");
        term.feed("Voice: tap Voice button to start/stop; Symbol + V also works (Settings)\r\n");
        term.feed("BusyBox core utilities are also available; type help for the full terminal list.\r\n");
        if (!pty.start(argv, rows, cols, home, env)) persistent_error = pty.error();
        std::string shell_state;
        bool finished = false;
        while (!screen::quit && !interrupted) {
            auto bytes = pty.read();
            if (!bytes.empty()) term.feed(bytes);
            for (uint32_t code; !screen::quit && !interrupted && (code = screen::take_key()) != 0;) {
                key(code);
                route_terminal_output(term.take_output());
            }
            if (screen::quit || interrupted) break;
            if (pending_ctrl_a && uint32_t(screen::tick() - pending_ctrl_a_at) > 1500) {
                send_terminal_text("\x01");
                pending_ctrl_a = false;
            }
            route_terminal_output(term.take_output());
            poll_voice_preview();
            if (voice_recording || voice_finishing) {
                if (!voice_final_pending && voice_recorder.poll()) finish_voice_recording();
            }
            if (voice_busy && voice_job.valid() &&
                voice_job.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                try {
                    const auto result = voice_job.get();
                    voice_busy = false;
                    if (pty.running()) send_terminal_text(result.text);
                    persistent_error = result.warning;
                } catch (const std::exception &e) {
                    voice_busy = false;
                    persistent_error = std::string("语音输入失败：") + e.what();
                }
            }
            pty.pump();
            if (term.output_overflow()) persistent_error = "Terminal reply queue overflow";
            if (!finished && !pty.running()) {
                // Drain bytes before cleanup closes master, preserving exec errors
                // and the last prompt/command output for inspection.
                for (int i = 0; i < 4; ++i) { auto last = pty.read(); if (last.empty()) break; term.feed(last); }
                int exit_status = pty.status();
                if (WIFEXITED(exit_status)) shell_state = "Shell exited (" + std::to_string(WEXITSTATUS(exit_status)) + ")";
                else shell_state = "Shell stopped";
                finished = true; pty.stop();
                // `exit` (or Ctrl-D) is the terminal's normal way to leave.
                // Once the interactive shell is gone, return to launcher.
                screen::quit = true;
            }
            if (!pty.error().empty()) persistent_error = pty.error();
            set_status(shell_state);
            if (term.take_dirty()) lv_obj_invalidate(canvas);
            lv_timer_handler();
            usleep(10000);
        }
        pty.stop();shell_pty=nullptr;ime.reset();
        lv_obj_clean(screen_root); model = nullptr;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "terminal: %s\n", error.what()); result = 1;
        lv_obj_clean(lv_screen_active()); model = nullptr;
    }
        voice_cancelled = true;
        voice_preview_cancelled = true;
        c1::cancel_requests();
        voice_recorder.stop();
        if (voice_job.valid()) voice_job.wait();
        if (voice_preview_job.valid()) voice_preview_job.wait();
        if (!voice_file.empty()) unlink(voice_file.c_str());
    shell_pty=nullptr; ime.reset();
    release_fonts(); screen::close(); return result;
}
