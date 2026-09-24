#include "terminal.hpp"
#include "pty.hpp"
#include "input.hpp"
#include "display.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
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
lv_obj_t *canvas = nullptr, *status = nullptr;
terminal::Terminal *model = nullptr;
terminal::Input input;
std::string persistent_error;
bool caps = false;
std::string last_status;

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
    else if (!shell_state.empty()) text = shell_state + "  |  Power: home";
    else {
        text = caps ? "CAPS  " : "abc   ";
        text += input.hint();
        if (model->history_offset()) text = "HISTORY -" + std::to_string(model->history_offset()) + "  " + text;
    }
    if (text != last_status) { lv_label_set_text(status, text.c_str()); last_status = std::move(text); }
}
void key(uint32_t code) {
    if(code==screen::KEY_FONT_UP||code==screen::KEY_FONT_DOWN){resize_font(code==screen::KEY_FONT_UP?2:-2);return;}
    if (code == screen::KEY_HOME) { screen::quit = true; return; }
    if (code == screen::KEY_MODE) { caps = screen::caps_lock(); return; }
    if (code == screen::KEY_SYMBOL) { input.symbol(); return; }
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
    default: if (code >= 32 && code < 0x10000) input.character(*model, code); break;
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
    if(regular)regular->fallback=nullptr;if(bold)bold->fallback=nullptr;
    for(auto*f:{regular,bold,cjk})if(f)lv_tiny_ttf_destroy(f);
    regular=next;bold=next_bold;cjk=next_cjk;regular->fallback=cjk;if(bold)bold->fallback=cjk;
    font_size=size;cell_width=cw;cell_height=ch;rows=new_rows;cols=new_cols;
    model->resize(rows,cols);last_resize=now;lv_obj_invalidate(canvas);
    std::fprintf(stderr,"[terminal] font=%d grid=%dx%d\n",font_size,cols,rows);
}
void release_fonts() {
    if (regular) regular->fallback = nullptr;
    if (bold) bold->fallback = nullptr;
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
        regular = font_file(assets + "JetBrainsMono-Regular.ttf", 16, 160);
        bold = font_file(assets + "JetBrainsMono-Bold.ttf", 16, 96);
        small = font_file(assets + "JetBrainsMono-Regular.ttf", 13, 96);
        cjk = font_file(root + "/shared/NotoSansSC-Regular.ttf", 16, 96);
        if (!regular || !small) throw std::runtime_error("Terminal JetBrains Mono font is missing");
        if (cjk) { regular->fallback = cjk; if (bold) bold->fallback = cjk; }
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
        lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(canvas, lv_color_hex(0x0c121c), 0);
        lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, 0);
        lv_obj_add_event_cb(canvas, draw, LV_EVENT_DRAW_MAIN, nullptr);
        status = lv_label_create(screen_root);
        lv_obj_set_pos(status, 0, body_height); lv_obj_set_size(status, 800, 32);
        lv_obj_set_style_bg_opa(status, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(status, lv_color_hex(0x1d2b3d), 0);
        lv_obj_set_style_text_color(status, lv_color_hex(0xd7e2ed), 0);
        lv_obj_set_style_text_font(status, small, 0);
        lv_obj_set_style_pad_left(status, 4, 0); lv_obj_set_style_pad_top(status, 6, 0);
        lv_label_set_long_mode(status, LV_LABEL_LONG_CLIP);
        caps = screen::caps_lock();
        std::string shell = environment("C1_TERMINAL_SHELL", "");
        if (shell.empty()) {
            for (const auto &candidate : {root + "/linux-tools/bin/bash", root + "/tools/bin/bash", std::string("/bin/bash"), std::string("/bin/sh")})
                if (access(candidate.c_str(), X_OK) == 0) { shell = candidate; break; }
        }
        const auto home = data + "/terminal";
        mkdir(data.c_str(), 0755); mkdir(home.c_str(), 0700);
        auto path = root + "/linux-tools/bin:" + root + "/tools/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin";
        std::string terminfo = root + "/linux-tools/share/terminfo", termname = "xterm-256color";
        if (!readable(terminfo + "/x/xterm-256color") && !readable(terminfo + "/78/xterm-256color")) {
            terminfo = assets + "terminfo"; termname = "c1max";
        }
        std::vector<std::string> env = {"TERM=" + termname, "TERMINFO=" + terminfo,
            "COLORTERM=truecolor", "PATH=" + path, "HOME=" + home, "SHELL=" + shell,
            "ENV=" + assets + "shellrc", "LC_ALL=C", "LANG=C", "PS1=\\w # ",
            "HISTFILE=" + home + "/history", "INPUTRC=" + assets + "inputrc"};
        // C.UTF-8 is not assumed to exist in the stock Buildroot glibc locale
        // archive. Output is always decoded as UTF-8; shell editing is locale-dependent.
        std::vector<std::string> argv{shell};
        if (shell.size() >= 4 && shell.compare(shell.size() - 4, 4, "bash") == 0) {
            argv.push_back("--noprofile"); argv.push_back("--rcfile"); argv.push_back(assets + "shellrc");
        }
        argv.push_back("-i");
        term.feed("\x1b[36mC1Max Terminal\x1b[0m  |  Symbol + C: interrupt\r\n");
        if (!pty.start(argv, rows, cols, home, env)) persistent_error = pty.error();
        std::string shell_state;
        bool finished = false;
        while (!screen::quit && !interrupted) {
            auto bytes = pty.read();
            if (!bytes.empty()) term.feed(bytes);
            for (uint32_t code; !screen::quit && !interrupted && (code = screen::take_key()) != 0;) key(code);
            if (screen::quit || interrupted) break;
            auto output = term.take_output();
            if (!output.empty() && !finished && !pty.send(output) && !pty.error().empty()) persistent_error = pty.error();
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
            }
            if (!pty.error().empty()) persistent_error = pty.error();
            set_status(shell_state);
            if (term.take_dirty()) lv_obj_invalidate(canvas);
            lv_timer_handler();
            usleep(10000);
        }
        pty.stop();shell_pty=nullptr;
        lv_obj_clean(screen_root); model = nullptr;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "terminal: %s\n", error.what()); result = 1;
        lv_obj_clean(lv_screen_active()); model = nullptr;
    }
    release_fonts(); screen::close(); return result;
}
