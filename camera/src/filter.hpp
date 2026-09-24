#pragma once
#include <cstdint>

namespace camera {
enum class Filter : unsigned { Original, Mono, Sepia, Warm, Cool, Faded, Count };
const char *filter_name(Filter filter);
uint32_t apply_filter(uint32_t argb, Filter filter, int x=0, int y=0, int width=0, int height=0);
}
