#pragma once
#include "filter.hpp"
#include "frame_art.hpp"
#include <algorithm>
#include <cstdint>

namespace camera {
enum class Aspect { Landscape, Portrait, Square, Wide, Original };
enum class Paper { None, White, Cream, Film, Count };
inline const char* aspect_name(Aspect a) {
    const char* names[] = {"4:3", "3:4", "1:1", "16:9", "原幅"};
    return names[std::min(unsigned(a), 4u)];
}
inline const char* paper_name(Paper p) {
    const char* names[] = {"无边框", "白相纸", "奶油纸", "黑胶片"};
    return names[std::min(unsigned(p), 3u)];
}
struct Composition {
    unsigned x = 0, y = 0, cw = 0, ch = 0;
    unsigned left = 0, top = 0, width = 0, height = 0;
    Paper paper = Paper::None;
};
// Ratios refer to the image aperture. Paper is added outside it, never
// stretched over it. Integer multiples make the requested ratio exact.
inline Composition compose(unsigned w, unsigned h, Aspect aspect, Paper paper) {
    if (!w || !h || w > 2048 || h > 2048 || unsigned(aspect) > 4 || unsigned(paper) >= 4) return {};
    Composition c; c.cw = w; c.ch = h; c.paper = paper;
    if (aspect != Aspect::Original) {
        const unsigned widths[] = {4, 3, 1, 16}, heights[] = {3, 4, 1, 9};
        unsigned rw = widths[unsigned(aspect)], rh = heights[unsigned(aspect)];
        unsigned scale = std::min(w / rw, h / rh);
        if (!scale) return {};
        c.cw = scale * rw; c.ch = scale * rh;
    }
    c.x = (w - c.cw) / 2; c.y = (h - c.ch) / 2;
    unsigned edge = paper == Paper::None ? 0 : std::max(2u, std::min(c.cw, c.ch) / 16);
    c.left = c.top = edge;
    c.width = c.cw + edge * 2;
    c.height = c.ch + edge * (paper == Paper::Film ? 2 : 4);
    return c;
}
// Both live view and JPEG use this exact crop/filter/paper pipeline.
// sample(x,y) supplies upright source pixels; no full-size RGB preview needed.
template<class Sample>
inline uint32_t composed_pixel(const Composition& c, Filter filter, unsigned x, unsigned y, Sample sample,
                               const FrameArt* art = nullptr) {
    if (x >= c.left && y >= c.top && x < c.left + c.cw && y < c.top + c.ch) {
        unsigned ix = x - c.left, iy = y - c.top;
        return apply_filter(sample(c.x + ix, c.y + iy), filter, ix, iy, c.cw, c.ch);
    }
    constexpr uint32_t matte = 0xff0b100f;
    return art ? alpha_over(matte, art->sample(x,y,c.width,c.height,c.left,c.top,c.cw,c.ch)) : matte;
}
}
