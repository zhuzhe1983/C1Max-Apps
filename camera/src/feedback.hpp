#pragma once
#include <cstdint>
namespace camera {
struct ShutterFeedback {
    bool active = false, cooling = false;
    uint32_t started = 0, finished = 0;
    bool begin(uint32_t now) {
        if (active || (cooling && uint32_t(now-finished) < 400)) return false;
        active = true; started = now; return true;
    }
    unsigned opacity(uint32_t now) const {
        uint32_t age = now-started;
        if (!active || age >= 180) return 0;
        return age < 35 ? 240 : (180-age)*240/145;
    }
    // Allow one complete display interval after fade-out before JPEG work.
    bool save_due(uint32_t now) const { return active && uint32_t(now-started) >= 240; }
    void complete(uint32_t now) { active = false; cooling = true; finished = now; }
};
}
