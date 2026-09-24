#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace subtitles {
// Subtitle time is always the source timeline, in 100 ns ticks.
struct Object {
    int width=0,height=0;
    size_t expected=0;
    std::vector<uint8_t> rle;
};
struct Placement {
    std::shared_ptr<const Object> object;
    int x=0,y=0,crop_x=0,crop_y=0,crop_w=0,crop_h=0;
};
struct Cue {
    int64_t start=0,end=0;
    std::string text;
    std::array<uint32_t,256> palette{};
    std::vector<Placement> objects;
};
struct Window { int64_t start=0,end=0;std::vector<Cue> cues; };
struct Image { int width=0,height=0;std::vector<uint32_t> pixels; };
Window vtt(const std::string&body,int64_t start,int64_t end);
Window pgs(const std::string&body,int64_t start,int64_t end);
std::string text_at(const Window&,int64_t ticks);
const Cue* bitmap_at(const Window&,int64_t ticks);
// Decode only the visible cue; retain other cues as compressed palette/RLE.
Image bitmap(const Cue&,int font_size=32);
bool pgs_codec(std::string codec);
bool text_codec(std::string codec);
}
