#pragma once
#include <string>

struct StreamOptions {
    bool width_fill=true;
    int subtitle=-1;
    int max_width() const { return 400; }
    // Fit spends pixels on the full frame; Width retains horizontal detail
    // before the panel crops vertically. Both stay inside the decoder bound.
    int max_height() const { return width_fill?288:170; }
    bool operator==(const StreamOptions &o) const { return width_fill==o.width_fill&&subtitle==o.subtitle; }
    bool operator!=(const StreamOptions &o) const { return !(*this==o); }
};
struct SubtitleTrack { int index=-1; std::string title,codec; };
