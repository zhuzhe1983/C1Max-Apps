#include "album.hpp"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_MAX_DIMENSIONS 2048
#include "stb_image.h"

namespace camera {
namespace {
bool legacy_sideways_photo(const std::string& path, int w, int h) {
    // Older releases saved unrotated 640x480 pixels under this exact naming
    // scheme. Correct their display without recompressing the original JPEG.
    // New 480x640 captures and unrelated/imported JPEGs stay as stored.
    if (w != 640 || h != 480) return false;
    const auto name = path.substr(path.find_last_of('/') + 1);
    if (name.size() != 34 || name.compare(0, 8, "instant-") != 0 ||
        name[16] != '-' || name[23] != '-' || name.compare(30, 4, ".jpg") != 0) return false;
    for (size_t i = 8; i < 30; ++i) {
        if (i == 16 || i == 23) continue;
        unsigned char c = name[i];
        if (i < 24 ? !std::isdigit(c) : !std::isalnum(c)) return false;
    }
    return true;
}
}
bool list_photos(const std::string& root, std::vector<Photo>& photos, std::string& error) {
    photos.clear(); error.clear();
    std::string folder = root + "/camera/photos";
    DIR* dir = opendir(folder.c_str());
    if (!dir) {
        if (errno == ENOENT) return true;
        error = "无法读取相册：" + std::string(strerror(errno)); return false;
    }
    errno = 0;
    while (auto* entry = readdir(dir)) {
        std::string name = entry->d_name;
        auto dot = name.find_last_of('.');
        if (name[0] != '.' && dot != std::string::npos) {
            std::string ext = name.substr(dot);
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
            if (ext == ".jpg" || ext == ".jpeg") {
                std::string path = folder + "/" + name; struct stat st{};
                if (lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode)) {
#ifdef __APPLE__
                    auto modified = st.st_mtimespec;
#else
                    auto modified = st.st_mtim;
#endif
                    photos.push_back({name, path, int64_t(modified.tv_sec) * 1000000000 + modified.tv_nsec,
                                      uint64_t(st.st_dev), uint64_t(st.st_ino)});
                }
            }
        }
        errno = 0;
    }
    int scan_error = errno; closedir(dir);
    if (scan_error) { error = "读取相册失败：" + std::string(strerror(scan_error)); photos.clear(); return false; }
    std::sort(photos.begin(), photos.end(), [](const Photo& a, const Photo& b) {
        return a.modified_ns != b.modified_ns ? a.modified_ns > b.modified_ns : a.name > b.name;
    });
    return true;
}

bool delete_photo(const std::string& root, const Photo& photo, std::string& error) {
    error.clear(); const auto folder = root + "/camera/photos";
    const auto& name = photo.name;
    auto dot = name.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : name.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (name.empty() || name[0] == '.' || name.find('/') != std::string::npos ||
        (ext != ".jpg" && ext != ".jpeg") || photo.path != folder + "/" + name) {
        error = "照片不在当前相册中"; return false;
    }
    int fd = open(folder.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) { error = "无法打开相册：" + std::string(strerror(errno)); return false; }
    struct stat st{};
    if (fstatat(fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
        error = "照片已不存在或无法访问"; close(fd); return false;
    }
#ifdef __APPLE__
    auto modified = st.st_mtimespec;
#else
    auto modified = st.st_mtim;
#endif
    if (!S_ISREG(st.st_mode) || uint64_t(st.st_dev) != photo.device || uint64_t(st.st_ino) != photo.inode ||
        int64_t(modified.tv_sec) * 1000000000 + modified.tv_nsec != photo.modified_ns) {
        error = "照片已发生变化，请重新打开相册"; close(fd); return false;
    }
    int result = unlinkat(fd, name.c_str(), 0); int saved_errno = errno;
    close(fd);
    if (result != 0) { error = "删除失败：" + std::string(strerror(saved_errno)); return false; }
    return true;
}

bool load_photo(const std::string& path, unsigned max_width, unsigned max_height,
                std::vector<uint32_t>& pixels, unsigned& width, unsigned& height, std::string& error) {
    pixels.clear(); width = height = 0; error.clear();
    if (!max_width || !max_height || max_width > 800 || max_height > 340) {
        error = "无效的相册显示尺寸"; return false;
    }
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) { error = "无法打开照片"; return false; }
    struct stat st{};
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 4 || st.st_size > 8 * 1024 * 1024) {
        close(fd); error = "照片为空或超过 8 MB"; return false;
    }
    std::vector<uint8_t> bytes(size_t(st.st_size)); size_t used = 0;
    while (used < bytes.size()) {
        ssize_t n = read(fd, bytes.data() + used, bytes.size() - used);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        used += n;
    }
    close(fd);
    // Our saved JPEGs always have a complete EOI marker. Reject interrupted writes.
    if (used != bytes.size() || bytes[0] != 0xff || bytes[1] != 0xd8 ||
        bytes[used-2] != 0xff || bytes[used-1] != 0xd9) {
        error = "照片文件不完整"; return false;
    }
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels) ||
        w < 1 || h < 1 || w > 2048 || h > 2048 || size_t(w) * h > 4 * 1024 * 1024) {
        error = "照片格式或尺寸不支持"; return false;
    }
    auto* source = stbi_load_from_memory(bytes.data(), int(bytes.size()), &w, &h, &channels, 3);
    if (!source) { error = "照片损坏，无法显示"; return false; }
    bool clockwise = legacy_sideways_photo(path, w, h);
    unsigned view_w = clockwise ? h : w, view_h = clockwise ? w : h;
    unsigned dw = view_w, dh = view_h;
    if (dw > max_width) { dh = std::max(1u, view_h * max_width / view_w); dw = max_width; }
    if (dh > max_height) { dw = std::max(1u, view_w * max_height / view_h); dh = max_height; }
    pixels.resize(size_t(dw) * dh);
    for (unsigned y = 0; y < dh; ++y) for (unsigned x = 0; x < dw; ++x) {
        unsigned rx = x * view_w / dw, ry = y * view_h / dh;
        unsigned sx = clockwise ? ry : rx, sy = clockwise ? unsigned(h) - 1 - rx : ry;
        auto* p = source + (size_t(sy) * w + sx) * 3;
        pixels[size_t(y) * dw + x] = 0xff000000u | (uint32_t(p[0]) << 16) | (uint32_t(p[1]) << 8) | p[2];
    }
    stbi_image_free(source); width = dw; height = dh; return true;
}
}
