#include "c1ime.hpp"

namespace c1ime {

struct Engine::Impl {};

Engine::Engine(std::string, std::string) : impl_(std::make_unique<Impl>()) {}
Engine::~Engine() = default;
bool Engine::initialize() { return true; }
bool Engine::ready() const { return true; }
const std::string &Engine::error() const { static const std::string empty; return empty; }
Mode Engine::mode() const { return Mode::English; }
State Engine::state() const { return State::Inactive; }
void Engine::toggle_mode() {}
bool Engine::input(char) { return false; }
bool Engine::backspace() { return false; }
void Engine::cancel() {}
void Engine::page_up() {}
void Engine::page_down() {}
std::string Engine::select(int) { return {}; }
std::string Engine::take_commit() { return {}; }
std::string Engine::buffer() const { return {}; }
std::vector<Candidate> Engine::candidates() const { return {}; }

} // namespace c1ime
