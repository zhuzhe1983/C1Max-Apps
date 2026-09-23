#include "terminal.hpp"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace terminal {
Terminal::Terminal(int rows, int cols, size_t history_limit)
    : rows_(rows), cols_(cols), history_limit_(history_limit) {
    if (rows < 1 || cols < 1 || rows > 200 || cols > 400 || history_limit > 2000)
        throw std::invalid_argument("terminal geometry/history is out of range");
    term_ = vterm_new(rows, cols);
    if (!term_) throw std::bad_alloc();
    vterm_set_utf8(term_, 1);
    screen_ = vterm_obtain_screen(term_);
    if (!screen_) { vterm_free(term_); term_ = nullptr; throw std::bad_alloc(); }
    static const VTermScreenCallbacks callbacks = {
        damage, moved, cursor_moved, property, nullptr, nullptr,
        push_line, nullptr, clear_history
    };
    vterm_screen_set_callbacks(screen_, &callbacks, this);
    vterm_output_set_callback(term_, output, this);
    vterm_screen_enable_altscreen(screen_, 1);
    vterm_screen_set_damage_merge(screen_, VTERM_DAMAGE_ROW);
    VTermColor fg{}, bg{};
    vterm_color_rgb(&fg, 0xd7, 0xe2, 0xed);
    vterm_color_rgb(&bg, 0x0c, 0x12, 0x1c);
    auto *state = vterm_obtain_state(term_);
    vterm_state_set_default_colors(state, &fg, &bg);
    vterm_state_set_bold_highbright(state, 1);
    vterm_screen_reset(screen_, 1);
}
Terminal::~Terminal() { if (term_) vterm_free(term_); }
void Terminal::feed(const char *bytes, size_t size) {
    vterm_input_write(term_, bytes, size);
    vterm_screen_flush_damage(screen_);
}
void Terminal::character(uint32_t c, VTermModifier mod) {
    live(); vterm_keyboard_unichar(term_, c, mod);
}
void Terminal::key(VTermKey key, VTermModifier mod) {
    live(); vterm_keyboard_key(term_, key, mod);
}
std::string Terminal::take_output() { std::string s; s.swap(output_); return s; }
VTermScreenCell Terminal::cell(int row, int col) const {
    VTermScreenCell result{};
    if (row < 0 || row >= rows_ || col < 0 || col >= cols_) return result;
    if (history_offset_) {
        const auto index = history_.size() - history_offset_ + size_t(row);
        if (index < history_.size()) return history_[index][size_t(col)];
        row = int(index - history_.size());
    }
    vterm_screen_get_cell(screen_, {row, col}, &result);
    return result;
}
VTermPos Terminal::cursor() const {
    VTermPos pos{}; vterm_state_get_cursorpos(vterm_obtain_state(term_), &pos); return pos;
}
uint32_t Terminal::rgb(VTermColor color) const {
    vterm_screen_convert_color_to_rgb(screen_, &color);
    return uint32_t(color.rgb.red) << 16 | uint32_t(color.rgb.green) << 8 | color.rgb.blue;
}
std::string Terminal::utf8(const VTermScreenCell &cell) {
    std::string text;
    for (auto c : cell.chars) {
        if (!c || c == UINT32_MAX) break; // wide-character continuation
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) c = 0xfffd;
        if (c < 0x80) text += char(c);
        else if (c < 0x800) { text += char(0xc0 | (c >> 6)); text += char(0x80 | (c & 63)); }
        else if (c < 0x10000) {
            text += char(0xe0 | (c >> 12)); text += char(0x80 | ((c >> 6) & 63)); text += char(0x80 | (c & 63));
        } else {
            text += char(0xf0 | (c >> 18)); text += char(0x80 | ((c >> 12) & 63));
            text += char(0x80 | ((c >> 6) & 63)); text += char(0x80 | (c & 63));
        }
    }
    return text;
}
void Terminal::scroll_history(int lines) {
    if (alternate_) return;
    auto next = std::clamp<int64_t>(int64_t(history_offset_) + lines, 0, int64_t(history_.size()));
    if (history_offset_ != size_t(next)) { history_offset_ = size_t(next); dirty_ = true; }
}
void Terminal::live() { if (history_offset_) { history_offset_ = 0; dirty_ = true; } }
bool Terminal::take_dirty() { bool changed = dirty_; dirty_ = false; return changed; }
int Terminal::damage(VTermRect, void *p) { static_cast<Terminal *>(p)->dirty_ = true; return 1; }
int Terminal::moved(VTermRect, VTermRect, void *p) { static_cast<Terminal *>(p)->dirty_ = true; return 1; }
int Terminal::cursor_moved(VTermPos, VTermPos, int visible, void *p) {
    auto &self = *static_cast<Terminal *>(p);
    self.cursor_visible_ = visible; self.dirty_ = true; return 1;
}
int Terminal::property(VTermProp prop, VTermValue *value, void *p) {
    auto &self = *static_cast<Terminal *>(p);
    if (prop == VTERM_PROP_CURSORVISIBLE) self.cursor_visible_ = value->boolean;
    if (prop == VTERM_PROP_ALTSCREEN) { self.alternate_ = value->boolean; self.history_offset_ = 0; }
    self.dirty_ = true;
    // Titles, bells, clipboard, image protocols and window control have no UI or OS side effects.
    return 1;
}
int Terminal::push_line(int cols, const VTermScreenCell *cells, void *p) {
    auto &self = *static_cast<Terminal *>(p);
    if (!self.history_limit_ || self.alternate_ || cols != self.cols_) return 1;
    self.history_.emplace_back(cells, cells + cols);
    if (self.history_offset_) ++self.history_offset_;
    if (self.history_.size() > self.history_limit_) self.history_.pop_front();
    self.history_offset_ = std::min(self.history_offset_, self.history_.size());
    self.dirty_ = true; return 1;
}
int Terminal::clear_history(void *p) {
    auto &self = *static_cast<Terminal *>(p);
    self.history_.clear(); self.history_offset_ = 0; self.dirty_ = true; return 1;
}
void Terminal::output(const char *bytes, size_t size, void *p) {
    auto &self = *static_cast<Terminal *>(p);
    // Bound replies even if a command continuously asks for terminal reports.
    if (size > 65536 - self.output_.size()) { self.output_overflow_ = true; return; }
    self.output_.append(bytes, size);
}
}
