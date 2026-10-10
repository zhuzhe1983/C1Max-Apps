#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

// A bounded, asynchronous raw-video bridge. All network/IPC transmission runs
// away from the emulator clock; never touch the V4L2 encoder.
namespace game_cast {
void start(const std::string &app, const std::string &title);
void stop();
// Reserve a capture slot (at most 20/sec). No allocation or network operation.
bool video_due();
// Input pixels are native-endian; wire pixels are always RGB565 little endian.
// Aspect is display aspect, independent of emulated pixel aspect. 16:9 is
// letterboxed into the fixed 320x240 transport, leaving no local key guides.
void frame_rgb555(const uint16_t *, unsigned width, unsigned height, size_t pitch,
                  unsigned aspect_num=4, unsigned aspect_den=3);
void frame_rgb565(const uint16_t *, unsigned width, unsigned height, size_t pitch,
                  unsigned aspect_num=4, unsigned aspect_den=3);
void frame_xrgb8888(const uint32_t *, unsigned width, unsigned height, size_t pitch,
                    unsigned aspect_num=4, unsigned aspect_den=3);
void audio(const int16_t *, size_t frames, unsigned channels, unsigned rate);
bool local_video();
bool local_audio();
// Nonempty during connection, remote-only display and failed-cast fallback.
std::string message();
}
