#include "timeline.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>

namespace {
constexpr int64_t second = INT64_C(10000000);
int failures = 0;

void expect(bool condition, const char *name) {
    if (!condition) { std::cerr << "FAIL: " << name << '\n'; ++failures; }
}
void expect_ticks(const PlaybackTimeline &timeline, int64_t expected, const char *name) {
    if (timeline.ticks() != expected) {
        std::cerr << "FAIL: " << name << ": " << timeline.ticks()
                  << " ticks, expected " << expected << '\n';
        ++failures;
    }
}
}

int main() {
    PlaybackTimeline time;
    expect(!time.has_time(), "default has no playback sample");
    expect_ticks(time, 0, "default starts at zero");

    time.reset(60 * second, 120 * second);
    time.set_origin(1.4);
    expect(!time.has_time(), "metadata is not a playback sample");
    expect_ticks(time, 60 * second, "seek target visible before first sample");
    expect(time.update(1.4), "accept first TS sample");
    expect_ticks(time, 60 * second, "strip nonzero TS origin after seek");
    expect(time.has_time(), "first valid sample marks clock available");
    expect(time.update(2.4), "accept second TS sample");
    expect_ticks(time, 61 * second, "advance one second after seek");

    // keep_force queries during pause must not move the playback position.
    for (int i = 0; i < 20; ++i) expect(time.update(2.4), "repeated pause sample is valid");
    expect_ticks(time, 61 * second, "pause queries do not advance time");
    expect(time.update(2.0), "old PTS is still a valid sample");
    expect_ticks(time, 61 * second, "out-of-order sample does not rewind stream");
    expect(time.update(3.4), "resume sample valid");
    expect_ticks(time, 62 * second, "resume clock advances from media PTS");

    time.reset(10 * second, 120 * second);
    expect(!time.has_time(), "seek reset discards old clock");
    expect_ticks(time, 10 * second, "backward seek can reduce absolute position");
    time.set_origin(1.4);
    time.update(2.4);
    expect_ticks(time, 11 * second, "backward seek uses its own TS origin");

    // The caller must discard old-process stdout; reset cannot identify which
    // process produced an otherwise valid PTS value.
    time.reset(25 * second, 0);
    time.update(18.5);
    expect_ticks(time, 25 * second, "unknown origin falls back to first sampled PTS");
    time.update(20.0);
    expect_ticks(time, 26 * second + second / 2, "fallback elapsed time");
    time.set_origin(18.0);
    expect_ticks(time, 27 * second, "late ID_START_TIME supersedes fallback");
    time.set_origin(0);
    expect_ticks(time, 27 * second, "repeated metadata does not replace explicit origin");

    time.reset(0, 10 * second);
    time.update(1.4);
    time.update(2.4);
    time.set_origin(1.8);
    expect_ticks(time, second, "late higher origin does not rewind published time");
    time.update(3.8);
    expect_ticks(time, 2 * second, "corrected clock catches up after late origin");

    time.reset(0, 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double invalid : {nan, inf, -inf, -1.0}) {
        time.set_origin(invalid);
        expect(!time.update(invalid), "reject invalid timestamp");
        expect(!time.has_time(), "invalid timestamp does not mark clock available");
        expect_ticks(time, 0, "invalid timestamp does not change position");
    }
    time.set_origin(0);
    expect(time.update(0), "zero is a valid timestamp");
    time.update(0.125);
    expect_ticks(time, second / 8, "subsecond precision retained");
    for (double invalid : {nan, inf, -inf, -0.01}) {
        expect(!time.update(invalid), "reject invalid timestamp after playback started");
        expect_ticks(time, second / 8, "invalid timestamp preserves last progress");
    }

    time.reset(3 * second, 5 * second);
    time.set_origin(1.4);
    time.update(9.4);
    expect_ticks(time, 5 * second, "clamp progress to known duration");
    time.update(1.4);
    expect_ticks(time, 5 * second, "clamped duration never rewinds within same stream");
    time.reset(100 * second, 5 * second);
    expect_ticks(time, 5 * second, "seek beyond end clamps to duration");
    time.reset(-100, -10);
    expect_ticks(time, 0, "negative reset input is sanitized");
    time.set_origin(0);
    time.update(1);
    expect_ticks(time, second, "nonpositive duration means unknown");

    // Real films exceed int32 ticks after only 214 seconds. Longer synthetic
    // timelines also prove no time_t/2038-style int32-seconds conversion occurs.
    time.reset(8 * 3600 * second, 12 * 3600 * second);
    time.set_origin(1.4);
    time.update(3601.4);
    expect_ticks(time, 9 * 3600 * second, "long-video ticks exceed int32 safely");
    constexpr int64_t beyond_2038 = (INT64_C(2147483647) + 123) * second;
    time.reset(beyond_2038, beyond_2038 + 3600 * second);
    time.set_origin(1.4);
    time.update(61.4);
    expect_ticks(time, beyond_2038 + 60 * second, "offset exceeds signed int32 seconds");

    const int64_t maximum = std::numeric_limits<int64_t>::max();
    time.reset(maximum - second, 0);
    time.set_origin(0);
    time.update(0.5);
    expect_ticks(time, maximum - second / 2, "adding elapsed near int64 limit is safe");
    time.update(2);
    expect_ticks(time, maximum, "unknown-duration overflow saturates at int64 max");
    time.reset(0, 0);
    time.set_origin(0);
    expect(time.update(std::numeric_limits<double>::max()), "huge finite timestamp accepted safely");
    expect_ticks(time, maximum, "huge timestamp cannot overflow float-to-int conversion");
    time.reset(0, 10 * second);
    time.set_origin(0);
    time.update(std::numeric_limits<double>::max());
    expect_ticks(time, 10 * second, "huge timestamp constrained by media duration");

    time.reset(0, 0);
    time.set_origin(1.376778);
    time.update(1.4);
    expect_ticks(time, 232220, "host sample.ts audio/video PTS offset retained");

    if (failures) return 1;
    std::cout << "PlaybackTimeline: seek, pause, TS origin, invalid PTS, duration and 64-bit bounds passed\n";
    return 0;
}
