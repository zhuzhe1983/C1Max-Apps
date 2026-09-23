#pragma once
#include "terminal.hpp"
#include <cstdint>

namespace terminal {
// Prefixes are one-shot and never alter the physical keyboard driver's map.
class Input {
public:
    enum class Mode { Text, Control, Navigation, Punctuation };
    void symbol() { mode_ = Mode((int(mode_) + 1) % 4); }
    void escape(Terminal &term) {
        if (mode_ != Mode::Text) mode_ = Mode::Text;
        else term.key(VTERM_KEY_ESCAPE);
    }
    void character(Terminal &term, uint32_t c) {
        auto mode = mode_; mode_ = Mode::Text;
        auto lower = c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
        if (mode == Mode::Text) { term.character(c); return; }
        if (mode == Mode::Control) {
            // Stock shells/editors expect legacy control bytes. libvterm's
            // modifier API deliberately emits CSI-u for Ctrl-I/J/M/[ instead.
            if (lower >= 'a' && lower <= 'z') term.character(lower & 0x1f);
            else if (c == ' ') term.key(VTERM_KEY_TAB);
            else if (c == '[' || c == '\\' || c == ']' || c == '^' || c == '_') term.character(c & 0x1f);
            return;
        }
        if (mode == Mode::Navigation) {
            switch (lower) {
            case 'w': term.key(VTERM_KEY_UP); break;
            case 'a': term.key(VTERM_KEY_LEFT); break;
            case 's': term.key(VTERM_KEY_DOWN); break;
            case 'd': term.key(VTERM_KEY_RIGHT); break;
            case 'q': term.key(VTERM_KEY_HOME); break;
            case 'e': term.key(VTERM_KEY_END); break;
            case 'z': term.key(VTERM_KEY_PAGEUP); break;
            case 'x': term.key(VTERM_KEY_PAGEDOWN); break;
            case 'b': term.key(VTERM_KEY_DEL); break;
            case ' ': term.key(VTERM_KEY_TAB); break;
            case 'r': term.scroll_history(term.rows() - 1); break;
            case 'f': term.scroll_history(1 - term.rows()); break;
            case 't': term.live(); break;
            default: break;
            }
            return;
        }
        static constexpr char keys[] = "qwertyuiopasdfgh";
        static constexpr char values[] = "=+_|\\\"'<>![]{}`^";
        for (size_t i = 0; i < sizeof(keys) - 1; ++i)
            if (lower == uint32_t(keys[i])) { term.character(uint8_t(values[i])); return; }
    }
    void key(Terminal &term, VTermKey key) {
        // Return cancels a pending prefix; Enter/Backspace keep their usual
        // meaning and cancel it too. No stale Ctrl survives an editing key.
        mode_ = Mode::Text; term.key(key);
    }
    Mode mode() const { return mode_; }
    const char *hint() const {
        switch (mode_) {
        case Mode::Control: return "CTRL: A-Z  |  Space Tab  |  Symbol again: navigation  |  Return cancel";
        case Mode::Navigation: return "NAV: WASD arrows  QE Home/End  ZX PgUp/Dn  B Del  RF history  T live";
        case Mode::Punctuation: return "SYM: Q= W+ E_ R| T\\ Y\" U' I< O> P! A[ S] D{ F} G` H^";
        default: return "Symbol: Ctrl  |  2x: navigation  |  3x: symbols  |  Return Esc  |  Power home";
        }
    }
private:
    Mode mode_ = Mode::Text;
};
}
