#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace camera {
inline uint32_t alpha_over(uint32_t under, uint32_t over) {
    unsigned a = over >> 24, inv = 255 - a;
    unsigned r = (((over >> 16) & 255) * a + ((under >> 16) & 255) * inv + 127) / 255;
    unsigned g = (((over >> 8) & 255) * a + ((under >> 8) & 255) * inv + 127) / 255;
    unsigned b = ((over & 255) * a + (under & 255) * inv + 127) / 255;
    return 0xff000000u | r << 16 | g << 8 | b;
}
class FrameArt {
    std::vector<uint32_t> pixels_;
    unsigned width_ = 0, height_ = 0, left_ = 0, top_ = 0, right_ = 0, bottom_ = 0;
    std::string path_;
public:
    bool load(const std::string& path, std::string& error);
    void clear() { std::vector<uint32_t>().swap(pixels_); path_.clear(); }
    bool ready() const { return !pixels_.empty(); }
    uint32_t sample(unsigned x, unsigned y, unsigned width, unsigned height,
                    unsigned left, unsigned top, unsigned image_width, unsigned image_height) const;
};
}
