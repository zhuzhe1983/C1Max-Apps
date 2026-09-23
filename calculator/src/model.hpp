#pragma once
#include "engine.hpp"
#include <string>

namespace calculator {
// UI-independent editing state, tested without LVGL or a framebuffer.
class Model {
public:
    void input(char ch);
    void evaluate();
    void backspace();
    void clear();
    void toggle_sign();
    const std::string &expression() const { return expression_; }
    const std::string &history() const { return history_; }
    const std::string &error() const { return error_; }
    bool evaluated() const { return evaluated_; }
    std::string display() const { return expression_.empty() ? "0" : expression_; }
private:
    std::string expression_, history_, error_;
    bool evaluated_ = false;
    void changed();
};
}
