#pragma once
#include "filter.hpp"
#include "composition.hpp"
#include <string>
#include <vector>
namespace camera {
bool save_photo(const std::string& data_root, const std::vector<uint32_t>& pixels,
                unsigned width, unsigned height, Filter filter,
                std::string& path, std::string& error,
                Aspect aspect = Aspect::Original, Paper paper = Paper::None, const FrameArt* art = nullptr);
}
