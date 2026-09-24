#pragma once
#include <cstdint>
#include <string>
#include <sys/types.h>
namespace camera {
class ShutterSound {
    pid_t pid_ = -1;
    uint32_t started_ = 0;
public:
    void play(const std::string& path, uint32_t now);
    void poll(uint32_t now);
    void stop();
    ~ShutterSound() { stop(); }
};
}
