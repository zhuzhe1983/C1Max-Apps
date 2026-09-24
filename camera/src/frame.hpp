#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace camera {
// The stock ispvideo driver reports NV12 bytesperline as width * 3 / 2,
// including chroma, despite storing ordinary tightly packed Y and UV planes.
// Limit the workaround to that exact driver/layout; padded NV12 stays padded.
inline unsigned nv12_stride(unsigned w, unsigned h, unsigned reported,
                            size_t size_image, bool ispvideo) {
    if (ispvideo && reported == w * 3 / 2 &&
        size_image == size_t(w) * h * 3 / 2) return w;
    return std::max(w, reported);
}
inline size_t nv12_size(unsigned stride, unsigned h) {
    return size_t(stride) * (h + h / 2);
}
// Caller validates the complete planes and coordinates once before sampling.
inline uint32_t nv12_pixel(const uint8_t* src, unsigned stride, unsigned h,
                           unsigned x, unsigned y, bool nv21) {
    const auto* cr = src + size_t(stride) * (h + y / 2);
    int c = 298 * std::max(0, int(src[size_t(y) * stride + x]) - 16);
    int u = int(cr[(x & ~1u) + (nv21 ? 1 : 0)]) - 128;
    int v = int(cr[(x & ~1u) + (nv21 ? 0 : 1)]) - 128;
    int r = std::clamp((c + 409 * v + 128) >> 8, 0, 255);
    int g = std::clamp((c - 100 * u - 208 * v + 128) >> 8, 0, 255);
    int b = std::clamp((c + 516 * u + 128) >> 8, 0, 255);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}
inline bool decode_nv12(const uint8_t *src, size_t used, size_t mapped,
                        unsigned w, unsigned h, unsigned stride, bool nv21,
                        std::vector<uint32_t>& rgb, bool clockwise = false) {
    if (!src || w < 2 || h < 2 || w > 2048 || h > 1944 ||
        (w & 1) || (h & 1) || stride < w || stride > 8192 ||
        used < nv12_size(stride, h) || mapped < nv12_size(stride, h)) return false;
    rgb.resize(size_t(w) * h);
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            // OV5648 is mounted sideways on the C1 Max. Rotate while converting
            // so preview and JPEG share upright pixels without a second buffer.
            size_t dst = clockwise ? size_t(x) * h + (h - 1 - y) : size_t(y) * w + x;
            rgb[dst] = nv12_pixel(src, stride, h, x, y, nv21);
        }
    }
    return true;
}
}
