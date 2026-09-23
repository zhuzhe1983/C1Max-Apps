#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Converts one MPlayer process's TS PTS clock into absolute Emby/Jellyfin ticks.
// Call reset for every restarted stream, including a backward seek. This class
// deliberately does not use time_t, wall-clock time or a 32-bit tick counter.
class PlaybackTimeline {
public:
    // duration <= 0 means unknown. An invalid start is clamped to the valid range.
    void reset(int64_t start, int64_t duration) {
        duration_ = std::max<int64_t>(0, duration);
        start_ = std::max<int64_t>(0, start);
        if (duration_ > 0) start_ = std::min(start_, duration_);
        ticks_ = start_;
        origin_ = latest_pts_ = 0;
        has_origin_ = explicit_origin_ = has_time_ = false;
    }

    // Prefer the first valid ID_START_TIME over the fallback first sampled PTS.
    // A late identifier can correct the fallback, but never moves this stream
    // backward. Repeated identifiers cannot replace an established explicit base.
    void set_origin(double pts) {
        if (!valid(pts) || explicit_origin_) return;
        origin_ = pts;
        has_origin_ = explicit_origin_ = true;
        if (has_time_) recalculate();
    }

    // true means a finite, non-negative sample was accepted, even if paused,
    // older than the previous sample, or already clamped to the media duration.
    bool update(double pts) {
        if (!valid(pts)) return false;
        if (!has_origin_) { origin_ = pts; has_origin_ = true; }
        if (!has_time_ || pts > latest_pts_) latest_pts_ = pts;
        has_time_ = true;
        recalculate();
        return true;
    }

    int64_t ticks() const { return ticks_; }
    bool has_time() const { return has_time_; }

private:
    static bool valid(double pts) { return std::isfinite(pts) && pts >= 0; }

    void recalculate() {
        const int64_t limit = duration_ > 0 ? duration_ : std::numeric_limits<int64_t>::max();
        const int64_t available = limit - start_;
        const long double elapsed = std::max<long double>(0,
            static_cast<long double>(latest_pts_) - static_cast<long double>(origin_));
        const long double offset = std::round(elapsed * 10000000.0L);
        // Compare before converting to int64_t. On MIPS, long double may have
        // only double precision: INT64_MAX itself can round to 2^63, so a cast
        // followed by a clamp would already have invoked undefined behavior.
        int64_t candidate = limit;
        if (std::isfinite(offset) && offset < static_cast<long double>(available))
            candidate = start_ + static_cast<int64_t>(offset);
        ticks_ = std::max(ticks_, candidate);
    }

    int64_t start_ = 0, duration_ = 0, ticks_ = 0;
    double origin_ = 0, latest_pts_ = 0;
    bool has_origin_ = false, explicit_origin_ = false, has_time_ = false;
};
