#pragma once

#include <sys/types.h>
#include <atomic>
#include <string>

namespace terminal {

struct VoiceConfig {
    bool enabled = false;
    bool include_context = true;
    bool live_preview = true;
    bool use_moonpilot = false;
    std::string endpoint;
    std::string model;
    std::string token;
    std::string chat_endpoint;
    std::string chat_model;
    std::string chat_token;
};

bool load_voice_config(const std::string &path, VoiceConfig &config, std::string &error);
bool save_voice_config(const std::string &path, const VoiceConfig &config, std::string &error);
bool load_moonpilot_config(const std::string &path, VoiceConfig &config, std::string &error);
std::string read_voice_snapshot(const std::string &path);

class VoiceRecorder {
public:
    VoiceRecorder() = default;
    ~VoiceRecorder();
    VoiceRecorder(const VoiceRecorder &) = delete;
    VoiceRecorder &operator=(const VoiceRecorder &) = delete;

    bool start(const std::string &path);
    void finish();
    bool poll();
    void stop();
    // Read a growing arecord WAV and repair its sizes for a preview request.
    std::string snapshot() const;
    bool active() const { return child_ > 0; }
    const std::string &path() const { return path_; }
    const std::string &error() const { return error_; }

private:
    pid_t child_ = -1;
    std::string path_;
    std::string error_;
};

// POST the device-side voice protocol. The server returns the final text to
// insert, after recognition and any context-aware polishing.
std::string request_voice(const VoiceConfig &config, const std::string &wav,
                          const std::string &window_context, int rows, int cols,
                          const std::atomic<bool> *cancel = nullptr, bool preview = false,
                          std::string *warning = nullptr);

} // namespace terminal
