#include "c1ime.hpp"

#include <rime_api.h>

#include <cerrno>
#include <cstring>
#include <filesystem>

namespace c1ime {
namespace fs = std::filesystem;

struct Engine::Impl {
    RimeApi *api = nullptr;
    RimeSessionId session = 0;
    std::string shared_dir;
    std::string user_dir;
    std::string error;
    Mode mode = Mode::English;
    bool ready = false;
};

Engine::Engine(std::string shared_data_dir, std::string user_data_dir)
    : impl_(std::make_unique<Impl>()) {
    impl_->shared_dir = std::move(shared_data_dir);
    impl_->user_dir = std::move(user_data_dir);
}

Engine::~Engine() {
    if (!impl_ || !impl_->api) return;
    if (impl_->session) impl_->api->destroy_session(impl_->session);
    impl_->session = 0;
    impl_->api->finalize();
    impl_->api = nullptr;
}

bool Engine::initialize() {
    if (!impl_) return false;
    if (impl_->ready) return true;
    std::error_code ec;
    if (impl_->shared_dir.empty() || !fs::is_directory(impl_->shared_dir, ec)) {
        impl_->error = "IME data directory is missing";
        return false;
    }
    fs::create_directories(fs::path(impl_->user_dir) / "build", ec);
    if (ec) {
        impl_->error = "IME data directory: " + ec.message();
        return false;
    }

    impl_->api = rime_get_api();
    if (!impl_->api) {
        impl_->error = "librime API unavailable";
        return false;
    }
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = impl_->shared_dir.c_str();
    traits.user_data_dir = impl_->user_dir.c_str();
    traits.distribution_name = "C1Max";
    traits.distribution_code_name = "c1max";
    traits.distribution_version = "1";
    traits.app_name = "rime.c1max";
    traits.min_log_level = 2;
    traits.log_dir = "";
    const auto staging = (fs::path(impl_->user_dir) / "build").string();
    const auto prebuilt = (fs::path(impl_->shared_dir) / "build").string();
    traits.staging_dir = staging.c_str();
    traits.prebuilt_data_dir = prebuilt.c_str();
    impl_->api->setup(&traits);
    impl_->api->initialize(nullptr);

    // The bundled package carries prebuilt prism/table files.  Building the
    // 3.6 MiB essay dictionary on the 103 MiB device can peak above 80 MiB,
    // so only fall back to Rime's maintenance compiler for development packs
    // that deliberately omit the prebuilt directory.
    const auto prebuilt_dir = fs::path(impl_->shared_dir) / "build";
    const bool has_prebuilt =
        fs::exists(prebuilt_dir / "luna_pinyin_simp.prism.bin", ec) &&
        fs::exists(prebuilt_dir / "luna_pinyin.table.bin", ec);
    if (!has_prebuilt && impl_->api->start_maintenance &&
        impl_->api->start_maintenance(True))
        impl_->api->join_maintenance_thread();
    if (!has_prebuilt && impl_->api->deploy_schema) {
        for (const auto &entry : fs::directory_iterator(impl_->shared_dir, ec)) {
            if (ec) break;
            const auto name = entry.path().filename().string();
            constexpr const char *suffix = ".schema.yaml";
            constexpr size_t suffix_length = 12;
            if (name.size() < suffix_length || name.rfind(suffix) != name.size() - suffix_length)
                continue;
            const auto schema_id = name.substr(0, name.size() - suffix_length);
            const auto prism = fs::path(staging) / (schema_id + ".prism.bin");
            if (!fs::exists(prism, ec)) impl_->api->deploy_schema(entry.path().c_str());
        }
    }
    impl_->session = impl_->api->create_session();
    if (!impl_->session) {
        impl_->error = "librime session creation failed";
        impl_->api->finalize();
        impl_->api = nullptr;
        return false;
    }
    if (!impl_->api->select_schema(impl_->session, "luna_pinyin_simp")) {
        impl_->error = "luna pinyin schema unavailable";
        impl_->api->destroy_session(impl_->session);
        impl_->session = 0;
        impl_->api->finalize();
        impl_->api = nullptr;
        return false;
    }
    impl_->ready = true;
    return true;
}

bool Engine::ready() const { return impl_ && impl_->ready; }
const std::string &Engine::error() const { static const std::string empty; return impl_ ? impl_->error : empty; }
Mode Engine::mode() const { return impl_ ? impl_->mode : Mode::English; }

State Engine::state() const {
    if (!ready()) return State::Inactive;
    RIME_STRUCT(RimeContext, context);
    if (!impl_->api->get_context(impl_->session, &context)) return State::Inactive;
    const bool selecting = context.menu.num_candidates > 0;
    const bool composing = context.composition.length > 0;
    impl_->api->free_context(&context);
    return selecting ? State::Selecting : (composing ? State::Composing : State::Inactive);
}

void Engine::toggle_mode() {
    if (!impl_) return;
    impl_->mode = impl_->mode == Mode::Chinese ? Mode::English : Mode::Chinese;
    if (ready()) impl_->api->clear_composition(impl_->session);
}

bool Engine::input(char ch) {
    if (!ready() || impl_->mode != Mode::Chinese) return false;
    return impl_->api->process_key(impl_->session, static_cast<unsigned char>(ch), 0) != 0;
}

bool Engine::backspace() {
    if (!ready()) return false;
    const bool handled = impl_->api->process_key(impl_->session, 0xff08, 0) != 0;
    return handled || state() != State::Inactive;
}

void Engine::cancel() {
    if (!ready()) return;
    impl_->api->process_key(impl_->session, 0xff1b, 0);
    impl_->api->clear_composition(impl_->session);
}

void Engine::page_up() { if (ready()) impl_->api->process_key(impl_->session, 0xff55, 0); }
void Engine::page_down() { if (ready()) impl_->api->process_key(impl_->session, 0xff56, 0); }

std::string Engine::select(int index) {
    if (!ready() || index < 0 || index > 9) return {};
    const int key = index == 9 ? '0' : ('1' + index);
    impl_->api->process_key(impl_->session, key, 0);
    return take_commit();
}

std::string Engine::take_commit() {
    if (!ready()) return {};
    RIME_STRUCT(RimeCommit, commit);
    if (!impl_->api->get_commit(impl_->session, &commit)) return {};
    std::string text = commit.text ? commit.text : "";
    impl_->api->free_commit(&commit);
    return text;
}

std::string Engine::buffer() const {
    if (!ready()) return {};
    RIME_STRUCT(RimeContext, context);
    if (!impl_->api->get_context(impl_->session, &context)) return {};
    std::string text = context.composition.preedit ? context.composition.preedit : "";
    impl_->api->free_context(&context);
    return text;
}

std::vector<Candidate> Engine::candidates() const {
    std::vector<Candidate> result;
    if (!ready()) return result;
    RIME_STRUCT(RimeContext, context);
    if (!impl_->api->get_context(impl_->session, &context)) return result;
    for (int i = 0; i < context.menu.num_candidates && i < 9; ++i) {
        result.push_back({context.menu.candidates[i].text ? context.menu.candidates[i].text : "",
                          context.menu.candidates[i].comment ? context.menu.candidates[i].comment : ""});
    }
    impl_->api->free_context(&context);
    return result;
}

} // namespace c1ime
