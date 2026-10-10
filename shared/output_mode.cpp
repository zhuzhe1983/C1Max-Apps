#include "output_mode.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace casting {
namespace {
bool valid_app(const std::string &app) {
    if (app.empty() || app.size() > 32 || app[0] < 'a' || app[0] > 'z') return false;
    return app.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos;
}
std::string directory() {
    const auto *data = std::getenv("C1_APPS_DATA");
    return std::string(data && *data ? data : "/storage/apps/data") + "/cast";
}
bool directory_ready(const std::string &dir) {
    if (mkdir(dir.c_str(), 0700) && errno != EEXIST) return false;
    struct stat st{};
    return !lstat(dir.c_str(), &st) && S_ISDIR(st.st_mode);
}
}

const char *output_mode_name(OutputMode mode) {
    switch (mode) {
    case OutputMode::Local: return "local";
    case OutputMode::Remote: return "remote";
    case OutputMode::Both: return "both";
    }
    return "local";
}
const char *output_mode_label(OutputMode mode) {
    switch (mode) {
    case OutputMode::Local: return "仅本机";
    case OutputMode::Remote: return "仅远端";
    case OutputMode::Both: return "本机＋远端";
    }
    return "仅本机";
}
OutputMode output_mode(const std::string &app) {
    if (!valid_app(app)) return OutputMode::Local;
    auto path = directory() + "/output-" + app;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return OutputMode::Local;
    struct stat st{};
    char buffer[16]{};
    ssize_t size = !fstat(fd, &st) && S_ISREG(st.st_mode) ? read(fd, buffer, sizeof buffer) : -1;
    close(fd);
    if (size == 7 && !memcmp(buffer, "remote\n", 7)) return OutputMode::Remote;
    if (size == 5 && !memcmp(buffer, "both\n", 5)) return OutputMode::Both;
    return OutputMode::Local;
}
bool set_output_mode(const std::string &app, OutputMode mode) {
    if (!valid_app(app) || (mode != OutputMode::Local && mode != OutputMode::Remote && mode != OutputMode::Both)) return false;
    const auto dir = directory();
    if (!directory_ready(dir)) return false;
    const auto path = dir + "/output-" + app;
    auto pattern = path + ".XXXXXX";
    std::vector<char> temp(pattern.begin(), pattern.end()); temp.push_back(0);
    int fd = mkstemp(temp.data());
    if (fd < 0) return false;
    const auto value = std::string(output_mode_name(mode)) + "\n";
    size_t at = 0;
    while (at < value.size()) {
        auto count = write(fd, value.data() + at, value.size() - at);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        at += count;
    }
    bool ok = at == value.size() && !fsync(fd);
    close(fd);
    if (ok) ok = !rename(temp.data(), path.c_str());
    if (!ok) { unlink(temp.data()); return false; }
    int parent = open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (parent < 0) return false;
    int synced; do { synced = fsync(parent); } while (synced && errno == EINTR);
    close(parent);
    return !synced;
}
}
