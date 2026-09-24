#pragma once
#include "vterm.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace terminal {
// UI-independent terminal state. All calls belong to the same event-loop thread.
class Terminal {
public:
    Terminal(int rows, int cols, size_t history_limit = 200);
    ~Terminal();
    Terminal(const Terminal &) = delete;
    Terminal &operator=(const Terminal &) = delete;
    void feed(const char *bytes, size_t size);
    void feed(const std::string &s) { feed(s.data(), s.size()); }
    void character(uint32_t character, VTermModifier modifier = VTERM_MOD_NONE);
    void key(VTermKey key, VTermModifier modifier = VTERM_MOD_NONE);
    std::string take_output();
    bool output_overflow() const { return output_overflow_; }
    VTermScreenCell cell(int row, int col) const;
    VTermPos cursor() const;
    bool cursor_visible() const { return cursor_visible_ && history_offset_ == 0; }
    bool alternate() const { return alternate_; }
    uint32_t rgb(VTermColor color) const;
    static std::string utf8(const VTermScreenCell &cell);
    void scroll_history(int lines);
    void live();
    void resize(int rows,int cols);
    size_t history_size() const { return history_.size(); }
    size_t history_offset() const { return history_offset_; }
    bool take_dirty();
    int rows() const { return rows_; }
    int cols() const { return cols_; }
private:
    VTerm *term_ = nullptr;
    VTermScreen *screen_ = nullptr;
    int rows_, cols_;
    size_t history_limit_, history_offset_ = 0;
    std::deque<std::vector<VTermScreenCell>> history_;
    std::string output_;
    bool dirty_ = true, cursor_visible_ = true, alternate_ = false, output_overflow_ = false;
    static int damage(VTermRect, void *);
    static int moved(VTermRect, VTermRect, void *);
    static int cursor_moved(VTermPos, VTermPos, int, void *);
    static int property(VTermProp, VTermValue *, void *);
    static int push_line(int, const VTermScreenCell *, void *);
    static int clear_history(void *);
    static void output(const char *, size_t, void *);
};
}
