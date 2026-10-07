#pragma once
#include "c1ime.hpp"
#include <lvgl.h>
#include <functional>
#include <memory>
#include <string>

namespace c1ime {
// Physical-keyboard editor. Owns its draft and Rime session; the application
// receives text only on confirmation. The top-layer panel survives app repaints.
class TextInput {
public:
    ~TextInput();
    void open(const std::string& app, const std::string& title, const std::string& value,
              unsigned max_chars, const lv_font_t* font,
              std::function<void(std::string)> apply, bool multiline=false, unsigned max_bytes=0);
    bool active() const { return root_ != nullptr; }
    bool key(uint32_t);
    void close();
    std::string text() const;
    std::string preedit() const;
    std::vector<Candidate> candidates() const;
private:
    lv_obj_t *root_=nullptr,*field_=nullptr,*mode_=nullptr,*preedit_=nullptr,*choices_=nullptr,*hint_=nullptr;
    std::unique_ptr<Engine> engine_;
    std::function<void(std::string)> apply_;
    std::string error_;
    unsigned max_bytes_=0;
    void refresh();
    void toggle();
    void choose(int);
    void confirm();
    void turn(int);
};
}
