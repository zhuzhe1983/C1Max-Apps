#include "photo.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace camera {
namespace {
bool directory(const std::string& path) {
    if (mkdir(path.c_str(), 0700) == 0) return true;
    struct stat st{};
    return errno == EEXIST && stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
void write_jpeg(void* context, void* data, int size) {
    fwrite(data, 1, size, static_cast<FILE*>(context));
}
}
bool save_photo(const std::string& root, const std::vector<uint32_t>& pixels,
                unsigned w, unsigned h, Filter filter, std::string& path, std::string& error,
                Aspect aspect, Paper paper, const FrameArt* art) {
    path.clear(); error.clear();
    if (paper != Paper::None && (!art || !art->ready())) { error = "相框尚未载入，请切换相纸重试"; return false; }
    const auto layout = compose(w, h, aspect, paper);
    if (!layout.width || !layout.height || layout.width > 2048 || layout.height > 2048 ||
        pixels.size() != size_t(w) * h || unsigned(filter) >= unsigned(Filter::Count)) {
        error = "Invalid photo frame"; return false;
    }
    const auto folder = root + "/camera/photos";
    if (!directory(root + "/camera") || !directory(folder)) {
        error = std::string("Cannot create photo folder: ") + strerror(errno); return false;
    }
    time_t now = time(nullptr); tm local{}; localtime_r(&now, &local);
    // Distinguish new landscape crops from the legacy sideways 640x480 JPEGs.
    char stamp[40]; strftime(stamp, sizeof stamp, "/print-%Y%m%d-%H%M%S-", &local);
    std::string temp = folder + stamp + "XXXXXX";
    // mkstemp gives each capture a unique name and 0600 permissions. Publish
    // the .jpg only after the writer and filesystem have accepted all bytes.
    std::vector<char> name(temp.begin(), temp.end()); name.push_back(0);
    int fd = mkstemp(name.data());
    if (fd < 0) { error = std::string("Cannot create photo: ") + strerror(errno); return false; }
    temp = name.data(); FILE* file = fdopen(fd, "wb");
    if (!file) { error = strerror(errno); close(fd); unlink(temp.c_str()); return false; }
    std::vector<uint8_t> data(size_t(layout.width) * layout.height * 3);
    for (size_t i = 0; i < data.size() / 3; ++i) {
        uint32_t p = composed_pixel(layout, filter, i % layout.width, i / layout.width,
                                   [&](unsigned x, unsigned y) { return pixels[size_t(y) * w + x]; }, art);
        data[i * 3] = p >> 16; data[i * 3 + 1] = p >> 8; data[i * 3 + 2] = p;
    }
    bool ok = stbi_write_jpg_to_func(write_jpeg, file, layout.width, layout.height, 3, data.data(), 92) != 0;
    if (fflush(file) != 0 || ferror(file)) ok = false;
    if (ok && fsync(fd) != 0) ok = false;
    if (fclose(file) != 0) ok = false;
    const auto final = temp + ".jpg";
    if (ok && link(temp.c_str(), final.c_str()) != 0) ok = false; // never overwrite an older photo
    unlink(temp.c_str());
    if (!ok) { error = "Photo write failed; check free space and folder permissions"; return false; }
    path = final; return true;
}
}
