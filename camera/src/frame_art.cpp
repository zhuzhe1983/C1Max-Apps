#include "frame_art.hpp"
#include "stb_image.h"
#include <algorithm>
#include <cstdio>

namespace camera {
bool FrameArt::load(const std::string& path, std::string& error) {
    error.clear();
    if (path == path_ && ready()) return true;
    clear();
    auto fail = [&]() { error = "相框资源读取失败，请重新安装拍立得"; return false; };
    FILE* meta = fopen((path + ".window").c_str(), "r");
    if (!meta) return fail();
    unsigned sw, sh, l, t, r, b;
    bool valid = fscanf(meta, "%u %u %u %u %u %u", &sw, &sh, &l, &t, &r, &b) == 6;
    fclose(meta);
    if (!valid || sw > 2048 || sh > 2048 || !l || !t || r <= l || b <= t || r >= sw || b >= sh) return fail();
    FILE* f = fopen((path + ".png").c_str(), "rb"); if (!f) return fail();
    bool seeked = fseek(f, 0, SEEK_END) == 0;
    long length = seeked ? ftell(f) : -1;
    if (length < 8 || length > 8*1024*1024 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return fail(); }
    std::vector<uint8_t> bytes(size_t(length), 0);
    bool read = fread(bytes.data(), 1, bytes.size(), f) == bytes.size(); fclose(f);
    int w = 0, h = 0, channels = 0;
    if (!read || !stbi_info_from_memory(bytes.data(), bytes.size(), &w, &h, &channels) ||
        w != int(sw) || h != int(sh) || channels != 4) return fail();
    auto* rgba = stbi_load_from_memory(bytes.data(), bytes.size(), &w, &h, &channels, 4);
    if (!rgba) return fail();
    // Keep one selected texture in a bounded ~1 MiB working buffer. The
    // original generated alpha PNG is packaged unchanged at full resolution.
    width_ = std::min(sw, 512u); height_ = std::min(sh, 512u);
    pixels_.resize(size_t(width_) * height_);
    for (unsigned y = 0; y < height_; ++y) for (unsigned x = 0; x < width_; ++x) {
        auto* p = rgba + (size_t(y * sh / height_) * sw + x * sw / width_) * 4;
        pixels_[size_t(y)*width_+x] = uint32_t(p[3]) << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | p[2];
    }
    stbi_image_free(rgba);
    left_ = l * width_ / sw; right_ = (sw-r) * width_ / sw;
    top_ = t * height_ / sh; bottom_ = (sh-b) * height_ / sh;
    path_ = path; return true;
}
static unsigned coordinate(unsigned p, unsigned size, unsigned before, unsigned after,
                           unsigned source, unsigned source_before, unsigned source_after) {
    if (p < before) return p * source_before / before;
    if (p >= size-after) return source-source_after + (p-(size-after))*source_after/after;
    return source_before + (p-before)*(source-source_before-source_after)/(size-before-after);
}
uint32_t FrameArt::sample(unsigned x, unsigned y, unsigned width, unsigned height,
                          unsigned left, unsigned top, unsigned iw, unsigned ih) const {
    if (!ready() || x >= width || y >= height || !left || !top || left+iw >= width || top+ih >= height) return 0;
    // Keep the entire aperture transparent, including incidental generation
    // specks. Edge/corner alpha and perforations remain from the PNG artwork.
    if (x >= left && y >= top && x < left+iw && y < top+ih) return 0;
    unsigned sx = coordinate(x, width, left, width-left-iw, width_, left_, right_);
    unsigned sy = coordinate(y, height, top, height-top-ih, height_, top_, bottom_);
    return pixels_[size_t(sy)*width_+sx];
}
}
