#pragma once

#include <memory>
#include <string>
#include <vector>

namespace c1ime {

struct Candidate {
    std::string text;
    std::string comment;
};

enum class Mode { English, Chinese };
enum class State { Inactive, Composing, Selecting };

// Small Rime adapter used by native C1Max applications.  The class deliberately
// keeps the Rime headers out of application code so applications can provide a
// no-op test implementation without linking the IME.
class Engine {
public:
    Engine(std::string shared_data_dir, std::string user_data_dir);
    ~Engine();

    bool initialize();
    bool ready() const;
    const std::string &error() const;
    Mode mode() const;
    State state() const;
    void toggle_mode();
    bool input(char ch);
    bool backspace();
    void cancel();
    void page_up();
    void page_down();
    std::string select(int index);
    std::string take_commit();
    std::string buffer() const;
    std::vector<Candidate> candidates() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace c1ime
