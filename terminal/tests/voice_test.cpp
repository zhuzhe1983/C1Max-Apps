#include "../src/voice.hpp"
#include <cassert>
#include <iostream>

int main(int argc, char **argv) {
    assert(argc == 2);
    terminal::VoiceConfig config;
    config.enabled = true;
    const std::string base = argv[1];
    config.model = "test-model";
    std::string wav(3244, '\0');
    wav.replace(0, 4, "RIFF"); wav.replace(8, 8, "WAVEfmt "); wav.replace(36, 4, "data");
    auto put = [&](size_t at, unsigned n, size_t bytes) {
        for (size_t i=0; i<bytes; ++i) wav[at+i] = static_cast<char>(n >> (8*i));
    };
    put(4, 3236, 4); put(16, 16, 4); put(20, 1, 2); put(22, 1, 2);
    put(24, 16000, 4); put(28, 32000, 4); put(32, 2, 2); put(34, 16, 2); put(40, 3200, 4);
    for (const char *path : {"/success", "/insert"}) {
        config.endpoint = base + path;
        assert(terminal::request_voice(config, wav, "prompt> git status", 14, 80) == "git status --short");
    }
    for (const char *path : {"/empty", "/newline", "/escape", "/oversize", "/badjson", "/error"}) {
        config.endpoint = base + path;
        bool rejected = false;
        try { terminal::request_voice(config, wav, "prompt> git status", 14, 80); }
        catch (const std::exception &) { rejected = true; }
        assert(rejected);
    }
    config.endpoint = base + "/preview";
    assert(terminal::request_voice(config, wav, "prompt> git status", 14, 80, nullptr, true).empty());
    config.endpoint = base + "/private";
    config.include_context = false;
    assert(terminal::request_voice(config, wav, "must not upload", 14, 80) == "git status --short");
    config.endpoint = base + "/success";
    config.include_context = true;
    std::atomic<bool> cancelled{true};
    bool rejected = false;
    try { terminal::request_voice(config, wav, "prompt> git status", 14, 80, &cancelled); }
    catch (const std::exception &) { rejected = true; }
    assert(rejected);
    std::cout << "PASS Terminal voice: request/preview, privacy opt-out, cancellation, malformed/error/empty/oversize/control rejection\n";
}
