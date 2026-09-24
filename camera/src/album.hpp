#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace camera {
struct Photo {
    std::string name, path;
    int64_t modified_ns = 0;
    uint64_t device = 0, inode = 0;
};
// Newest first. Missing photo directories are an empty album.
bool list_photos(const std::string& root, std::vector<Photo>& photos, std::string& error);
// Delete only the selected, unchanged regular file inside this album.
bool delete_photo(const std::string& root, const Photo& photo, std::string& error);
// Decodes one bounded JPEG and fits its complete image inside the viewfinder.
bool load_photo(const std::string& path, unsigned max_width, unsigned max_height,
                std::vector<uint32_t>& pixels, unsigned& width, unsigned& height,
                std::string& error);
}
