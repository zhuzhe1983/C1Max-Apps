#include "terminal.hpp"
#include "input.hpp"
#include <cassert>
#include <iostream>
#include <string>

using terminal::Terminal;
std::string line(Terminal &t, int row) {
    std::string s;
    for (int col = 0; col < t.cols(); ++col) {
        auto cell = t.cell(row, col);
        s += cell.chars[0] ? Terminal::utf8(cell) : " ";
    }
    return s;
}
int main() {
    {
        Terminal t(4, 12);
        t.feed("abcdef\rXY\x1b[K");
        assert(line(t, 0) == "XY          ");
        t.feed("\x1b[3;5Hq\x1b[2D!");
        assert(t.cell(2, 4).chars[0] == 'q');
        assert(t.cell(2, 3).chars[0] == '!');
        t.feed("\x1b[2J\x1b[H");
        assert(line(t, 0) == std::string(12, ' '));
        assert(t.cursor().row == 0 && t.cursor().col == 0);
        t.feed("\x1b[38;2;12;34;56m\x1b[48;5;196m\x1b[1;4;7mC");
        auto cell = t.cell(0, 0);
        assert(t.rgb(cell.fg) == 0x0c2238 && t.rgb(cell.bg) == 0xff0000);
        assert(cell.attrs.bold && cell.attrs.underline && cell.attrs.reverse);
    }
    {
        Terminal t(4, 12);
        // Split both UTF-8 and ANSI at arbitrary PTY read boundaries.
        const std::string input = "A中文e\xcc\x81\x1b[2;1Hz";
        for (char c : input) t.feed(&c, 1);
        assert(t.cell(0, 1).chars[0] == 0x4e2d && t.cell(0, 1).width == 2);
        assert(t.cell(0, 2).chars[0] == UINT32_MAX);
        assert(t.cell(0, 3).chars[0] == 0x6587 && t.cell(0, 3).width == 2);
        assert(t.cell(0, 5).chars[0] == 'e' && t.cell(0, 5).chars[1] == 0x301);
        assert(Terminal::utf8(t.cell(0, 5)) == "e\xcc\x81");
        assert(t.cell(1, 0).chars[0] == 'z');
        t.feed("\x1b[?1049h\x1b[Halternate");
        assert(t.alternate() && line(t, 0).substr(0, 9) == "alternate");
        t.feed("\x1b[?1049l");
        assert(!t.alternate() && t.cell(0, 1).chars[0] == 0x4e2d);
        t.feed("\x1b[?25l"); assert(!t.cursor_visible());
        t.feed("\x1b[?25h"); assert(t.cursor_visible());
    }
    {
        Terminal t(3, 8, 4);
        for (int i = 0; i < 20; ++i) t.feed(std::to_string(i) + "\r\n");
        assert(t.history_size() == 4);
        auto live = line(t, 0);
        t.scroll_history(100);
        assert(t.history_offset() == 4 && !t.cursor_visible());
        assert(line(t, 0) != live);
        t.feed("new\r\n");
        assert(t.history_size() == 4 && t.history_offset() == 4);
        t.character('x'); assert(t.history_offset() == 0);
        t.feed("\x1b[3J"); assert(t.history_size() == 0);
        t.feed("\x1b[?1049h");
        for (int i = 0; i < 20; ++i) t.feed("alt\r\n");
        assert(t.history_size() == 0);
    }
    {
        Terminal t(4, 12);
        t.feed("\x1b[2;3H\x1b[6n");
        assert(t.take_output() == "\x1b[2;3R");
        t.key(VTERM_KEY_UP); assert(t.take_output() == "\x1b[A");
        t.feed("\x1b[?1h");
        t.key(VTERM_KEY_UP); assert(t.take_output() == "\x1bOA");
        t.character('c', VTERM_MOD_CTRL); assert(t.take_output() == std::string(1, 3));
        t.key(VTERM_KEY_BACKSPACE); assert(t.take_output() == std::string(1, 127));
        t.key(VTERM_KEY_ENTER); assert(t.take_output() == "\r");
        // A hostile OSC title/clipboard must never become typed shell input.
        t.feed("\x1b]2;$(touch /tmp/never)\a\x1b]52;c;cm0gLXJmIC8=\a");
        assert(t.take_output().empty());
    }
    {
        Terminal t(4, 12);
        terminal::Input input;
        for (char c = 'a'; c <= 'z'; ++c) {
            input.symbol(); input.character(t, c);
            assert(t.take_output() == std::string(1, char(c - 'a' + 1)));
        }
        input.symbol(); input.character(t, ' '); assert(t.take_output() == "\t");
        input.symbol(); input.symbol(); input.character(t, 'w'); assert(t.take_output() == "\x1b[A");
        input.symbol(); input.symbol(); input.symbol(); input.character(t, 'r'); assert(t.take_output() == "|");
        input.symbol(); input.symbol(); input.symbol(); input.character(t, 'y'); assert(t.take_output() == "\"");
        input.symbol(); input.escape(t); assert(t.take_output().empty());
        input.escape(t); assert(t.take_output() == "\x1b");
        input.character(t, 'A'); assert(t.take_output() == "A");
    }
    {
        Terminal t(4, 12);
        for (int i = 0; i < 20000; ++i) t.feed("\x1b[6n");
        assert(t.output_overflow() && t.take_output().size() <= 65536);
    }
    std::cout << "terminal parser/input: ANSI, UTF-8, colors, alternate screen, history, replies and keyboard passed\n";
}
