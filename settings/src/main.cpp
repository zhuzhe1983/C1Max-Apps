#include "display.hpp"
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#include <tinyalsa/mixer.h>
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdarg>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <limits.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace {
volatile sig_atomic_t interrupted = 0;
void on_signal(int) { interrupted = 1; }

constexpr const char *kVersion = "0.2.0";
constexpr const char *kBacklight = "/sys/class/backlight/backlight/brightness";
constexpr const char *kBacklightMax = "/sys/class/backlight/backlight/max_brightness";

// ---------------------------------------------------------------- utilities

std::string read_file(const char *path, size_t limit = 65536) {
    std::string out;
    int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return out;
    char buf[1024];
    while (out.size() < limit) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n <= 0) break;
        out.append(buf, static_cast<size_t>(n));
    }
    ::close(fd);
    return out;
}

bool write_file(const std::string &path, const std::string &text) {
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0) return false;
    bool ok = ::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size());
    ::close(fd);
    return ok;
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) ++i;
    return s.substr(i);
}

std::string first_line(std::string s) {
    auto n = s.find('\n');
    if (n != std::string::npos) s.resize(n);
    return trim(s);
}

std::string sysfs(const char *path) { return first_line(read_file(path, 128)); }

long to_long(const std::string &s, long fallback) {
    if (s.empty()) return fallback;
    char *end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    return end == s.c_str() ? fallback : v;
}

std::string format(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
std::string format(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    return buf;
}

std::string data_dir() {
    const char *p = std::getenv("C1_APPS_DATA");
    return std::string(p && *p ? p : "/storage/apps/data") + "/settings";
}

std::string apps_data_dir() {
    const char *p = std::getenv("C1_APPS_DATA");
    return p && *p ? p : "/storage/apps/data";
}

std::string apps_root_dir() {
    const char *p = std::getenv("C1_APPS_ROOT");
    return p && *p ? p : "/storage/apps/current";
}

std::string sshd_data_dir() { return apps_data_dir() + "/terminal/dropbear"; }
std::string sshd_enabled_file() { return sshd_data_dir() + "/enabled"; }
std::string sshd_pid_file() { return sshd_data_dir() + "/dropbear.pid"; }

bool sshd_running() {
    const std::string text = first_line(read_file(sshd_pid_file().c_str(), 32));
    if (text.empty()) return false;
    char *end = nullptr;
    const long pid = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || *end || pid <= 1) return false;
    if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno != EPERM) return false;
    char executable[PATH_MAX];
    const std::string proc_exe = "/proc/" + std::to_string(pid) + "/exe";
    const ssize_t length = ::readlink(proc_exe.c_str(), executable, sizeof executable - 1);
    if (length <= 0) return false;
    executable[length] = '\0';
    const std::string path(executable);
    constexpr const char *suffix = "/linux-tools/bin/dropbear";
    const size_t suffix_length = std::strlen(suffix);
    return path.size() >= suffix_length && path.compare(path.size() - suffix_length, suffix_length, suffix) == 0;
}

bool sshd_auto_start() {
    return first_line(read_file(sshd_enabled_file().c_str(), 16)) == "1";
}

// argv only. Never concatenate SSID or password into a shell string.
int run_argv(const std::vector<std::string> &args, std::string *out, int timeout_ms = 2500) {
    if (args.empty()) return -1;
    int pipefd[2] = {-1, -1};
    if (out && ::pipe(pipefd) != 0) return -1;
    std::vector<char *> av;
    av.reserve(args.size() + 1);
    for (auto &a : args) av.push_back(const_cast<char *>(a.c_str()));
    av.push_back(nullptr);
    pid_t pid = ::fork();
    if (pid < 0) {
        if (out) { ::close(pipefd[0]); ::close(pipefd[1]); }
        return -1;
    }
    if (pid == 0) {
        if (out) {
            ::dup2(pipefd[1], STDOUT_FILENO);
            ::close(pipefd[0]);
            ::close(pipefd[1]);
        }
        int devnull = ::open("/dev/null", O_RDWR | O_CLOEXEC);
        if (devnull >= 0) {
            if (!out) ::dup2(devnull, STDOUT_FILENO);
            ::dup2(devnull, STDERR_FILENO);
            if (devnull > 2) ::close(devnull);
        }
        ::execvp(av[0], av.data());
        _exit(127);
    }
    if (out) {
        ::close(pipefd[1]);
        int flags = ::fcntl(pipefd[0], F_GETFL, 0);
        if (flags >= 0) ::fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }
    std::string captured;
    int status = 0;
    int waited = 0;
    bool done = false;
    while (waited < timeout_ms) {
        if (out) {
            char buf[512];
            ssize_t n;
            while ((n = ::read(pipefd[0], buf, sizeof buf)) > 0) captured.append(buf, static_cast<size_t>(n));
        }
        pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) { done = true; break; }
        ::usleep(5 * 1000);
        waited += 5;
    }
    if (!done) {
        ::kill(pid, SIGKILL);
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        if (out) ::close(pipefd[0]);
        return -1;
    }
    if (out) {
        char buf[512];
        ssize_t n;
        while ((n = ::read(pipefd[0], buf, sizeof buf)) > 0) captured.append(buf, static_cast<size_t>(n));
        ::close(pipefd[0]);
        if (captured.size() > 24000) captured.resize(24000);
        *out = std::move(captured);
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

std::string prop(const char *name) {
    std::string out;
    run_argv({"/usr/bin/getprop", name}, &out, 1500);
    return first_line(out);
}

void set_prop(const char *name, const std::string &value) {
    run_argv({"/usr/bin/setprop", name, value}, nullptr, 1500);
}

std::string value_after(const std::string &text, const char *key) {
    std::istringstream in(text);
    std::string line;
    const std::string prefix = std::string(key) + "=";
    while (std::getline(in, line)) {
        if (line.compare(0, prefix.size(), prefix) == 0) return trim(line.substr(prefix.size()));
    }
    return {};
}

std::vector<std::string> split_tabs(const std::string &line) {
    std::vector<std::string> col;
    std::string cur;
    for (char c : line) {
        if (c == '\t') { col.push_back(cur); cur.clear(); }
        else if (c != '\r') cur.push_back(c);
    }
    col.push_back(cur);
    return col;
}

// wpa_supplicant prints SSIDs with printf_encode(): \xNN, \\, \" and \e/\n/\r/\t.
std::string wpa_unescape(const std::string &s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) { out.push_back(s[i]); continue; }
        char c = s[++i];
        if (c == 'x' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out.push_back(static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16)));
            i += 2;
        } else if (c == 'n') out.push_back('\n');
        else if (c == 'r') out.push_back('\r');
        else if (c == 't') out.push_back('\t');
        else if (c == 'e') out.push_back('\033');
        else out.push_back(c);
    }
    return out;
}

std::string hex_encode(const std::string &s) {
    static const char digits[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) { out.push_back(digits[c >> 4]); out.push_back(digits[c & 15]); }
    return out;
}

bool valid_utf8(const std::string &s) {
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        int n = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (n < 0 || i + static_cast<size_t>(n) >= s.size()) return false;
        for (int k = 1; k <= n; ++k)
            if ((static_cast<unsigned char>(s[i + k]) >> 6) != 2) return false;
        if (c < 0x20 && c != '\t') return false;
        i += n + 1;
    }
    return true;
}

// Show readable names; fall back to the escaped form for binary SSIDs.
std::string display_ssid(const std::string &raw) {
    if (raw.empty()) return "（隐藏网络）";
    return valid_utf8(raw) ? raw : hex_encode(raw);
}

std::string human_bytes(unsigned long long bytes) {
    const double gb = 1024.0 * 1024.0 * 1024.0;
    if (bytes >= gb) return format("%.1f GB", bytes / gb);
    return format("%.0f MB", bytes / (1024.0 * 1024.0));
}

std::string storage_text(const char *path) {
    struct statvfs st{};
    if (::statvfs(path, &st) != 0) return "未知";
    unsigned long long total = static_cast<unsigned long long>(st.f_blocks) * st.f_frsize;
    unsigned long long avail = static_cast<unsigned long long>(st.f_bavail) * st.f_frsize;
    return "可用 " + human_bytes(avail) + " / 共 " + human_bytes(total);
}

std::string duration_text(long ms) {
    if (ms <= 0) return "永不";
    if (ms < 60000) return format("%ld 秒", ms / 1000);
    return format("%ld 分钟", ms / 60000);
}

// ------------------------------------------------------------------- Wi-Fi

struct WifiStatus {
    std::string state, ssid, ip, id;
    int rssi = 0;
};

struct SavedNet {
    std::string id, ssid;
    bool current = false, disabled = false, temp_disabled = false;
};

struct ScanNet {
    std::string ssid;
    int level = -100;
    bool secure = false, eap = false, wep = false;
};

int wpa(const std::vector<std::string> &rest, std::string *out = nullptr) {
    std::vector<std::string> args = {"/usr/sbin/wpa_cli", "-i", "wlan0"};
    args.insert(args.end(), rest.begin(), rest.end());
    int rc = run_argv(args, out, 2500);
    if (rc == 127) {
        args[0] = "/usr/bin/wpa_cli";
        rc = run_argv(args, out, 2500);
    }
    return rc;
}

bool wpa_ok(const std::vector<std::string> &rest) {
    std::string out;
    return wpa(rest, &out) == 0 && first_line(out) == "OK";
}

WifiStatus read_wifi() {
    WifiStatus w;
    std::string st;
    wpa({"status"}, &st);
    w.state = value_after(st, "wpa_state");
    w.ssid = wpa_unescape(value_after(st, "ssid"));
    w.ip = value_after(st, "ip_address");
    w.id = value_after(st, "id");
    if (w.state == "COMPLETED") {
        std::string poll;
        wpa({"signal_poll"}, &poll);
        w.rssi = static_cast<int>(to_long(value_after(poll, "RSSI"), 0));
    }
    return w;
}

std::vector<SavedNet> read_saved(bool *valid = nullptr) {
    std::vector<SavedNet> out;
    std::string raw;
    const bool good = wpa({"list_networks"}, &raw) == 0 && raw.find("network id / ssid / bssid / flags") == 0;
    if (valid) *valid = good;
    if (!good) return out;
    std::istringstream in(raw);
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        auto col = split_tabs(line);
        if (col.size() < 2 || col[0].empty() || !std::isdigit(static_cast<unsigned char>(col[0][0]))) continue;
        SavedNet n;
        n.id = col[0];
        n.ssid = wpa_unescape(col[1]);
        std::string flags = col.size() > 3 ? col[3] : "";
        n.current = flags.find("[CURRENT]") != std::string::npos;
        n.disabled = flags.find("[DISABLED]") != std::string::npos;
        n.temp_disabled = flags.find("[TEMP-DISABLED]") != std::string::npos;
        out.push_back(std::move(n));
    }
    return out;
}

std::vector<ScanNet> read_scan() {
    std::vector<ScanNet> out;
    std::string raw;
    wpa({"scan_results"}, &raw);
    std::istringstream in(raw);
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        auto col = split_tabs(line);
        if (col.size() < 5) continue;
        std::string ssid = col[4];
        for (size_t i = 5; i < col.size(); ++i) ssid += "\t" + col[i];
        ssid = wpa_unescape(ssid);
        if (ssid.empty() || ssid.find('\0') != std::string::npos) continue;
        ScanNet n;
        n.ssid = ssid;
        n.level = static_cast<int>(to_long(col[2], -100));
        const std::string &flags = col[3];
        n.eap = flags.find("EAP") != std::string::npos;
        n.wep = flags.find("WEP") != std::string::npos;
        n.secure = n.eap || n.wep || flags.find("WPA") != std::string::npos || flags.find("SAE") != std::string::npos;
        auto same = std::find_if(out.begin(), out.end(), [&](const ScanNet &o) { return o.ssid == n.ssid; });
        if (same == out.end()) out.push_back(std::move(n));
        else if (n.level > same->level) same->level = n.level;
    }
    std::sort(out.begin(), out.end(), [](const ScanNet &a, const ScanNet &b) { return a.level > b.level; });
    if (out.size() > 20) out.resize(20);
    return out;
}

const char *signal_word(int dbm) {
    if (dbm == 0) return "";
    if (dbm >= -55) return "强";
    if (dbm >= -67) return "较强";
    if (dbm >= -77) return "一般";
    return "弱";
}

// ------------------------------------------------------------- hardware

struct Mixer {
    struct mixer *mixer = nullptr;
    struct mixer_ctl *ctl = nullptr;
    int min = 0, max = 255;
    void open() {
        mixer = mixer_open(0);
        ctl = mixer ? mixer_get_ctl_by_name(mixer, "softvolume") : nullptr;
        if (ctl) {
            min = mixer_ctl_get_range_min(ctl);
            max = mixer_ctl_get_range_max(ctl);
            if (max <= min) ctl = nullptr;
        }
    }
    void close() { if (mixer) mixer_close(mixer); mixer = nullptr; ctl = nullptr; }
    int percent() const {
        if (!ctl) return -1;
        int v = mixer_ctl_get_value(ctl, 0);
        return static_cast<int>(((v - min) * 100L + (max - min) / 2) / (max - min));
    }
    void set_percent(int pct) {
        if (!ctl) return;
        int v = min + static_cast<int>((std::clamp(pct, 0, 100) * static_cast<long>(max - min) + 50) / 100);
        unsigned n = mixer_ctl_get_num_values(ctl);
        for (unsigned i = 0; i < n; ++i) mixer_ctl_set_value(ctl, i, v);
    }
} audio;

int backlight_max() { return std::max(1L, to_long(sysfs(kBacklightMax), 15)); }
int backlight_level() { return static_cast<int>(to_long(sysfs(kBacklight), backlight_max())); }

void set_backlight(int level) {
    const int mx = backlight_max();
    level = std::clamp(level, 1, mx);
    write_file(kBacklight, std::to_string(level) + "\n");
    // Same pair the stock UI writes: sysfs now, percent for PowerManager wakes.
    set_prop("sys.backlight.percent", std::to_string((level * 100 + mx / 2) / mx));
    ::mkdir(data_dir().c_str(), 0700);
    write_file(data_dir() + "/brightness", std::to_string(level) + "\n");
}

std::string battery_status_text(const std::string &st) {
    if (st == "Charging") return "充电中";
    if (st == "Discharging") return "使用电池";
    if (st == "Full") return "已充满";
    if (st == "Not charging") return "已接电源，未充电";
    return st.empty() ? "未知" : st;
}

// --------------------------------------------------------------- model

enum class Kind { Header, Info, Note, Action, Slider, Choice, Input, Meter };

struct Item {
    Kind kind = Kind::Info;
    std::string key, label, value;
    int lo = 0, hi = 100, step = 1, cur = 0;   // Slider / Meter
    std::vector<std::string> options;          // Choice
    int option = 0;
    bool accent = false, danger = false;
    std::function<void()> on_enter;
    std::function<void(int)> on_change;        // Slider value / Choice index
    lv_obj_t *row = nullptr, *name = nullptr, *val = nullptr, *bar = nullptr;
    bool focusable() const { return kind != Kind::Header && kind != Kind::Note; }
};

Item header(const std::string &text) { Item i; i.kind = Kind::Header; i.key = "h:" + text; i.label = text; return i; }
Item info(const std::string &label, const std::string &value) { Item i; i.kind = Kind::Info; i.key = "i:" + label; i.label = label; i.value = value; return i; }
Item note(const std::string &key, const std::string &text) { Item i; i.kind = Kind::Note; i.key = "n:" + key; i.label = text; return i; }
Item action(const std::string &key, const std::string &label, const std::string &value, std::function<void()> fn) {
    Item i; i.kind = Kind::Action; i.key = "a:" + key; i.label = label; i.value = value; i.on_enter = std::move(fn); return i;
}

enum class Section { Wifi, Display, Sound, Usb, Ssh, Battery, About };
constexpr int kSections = 7;
const char *const kSectionIcon[] = {LV_SYMBOL_WIFI, LV_SYMBOL_IMAGE, LV_SYMBOL_VOLUME_MAX, LV_SYMBOL_USB,
                                    LV_SYMBOL_SHUFFLE, LV_SYMBOL_BATTERY_FULL, LV_SYMBOL_LIST};
const char *const kSectionName[] = {"WLAN", "显示与熄屏", "声音", "USB", "SSH 服务", "电池", "关于本机"};

// Sheets replace the list temporarily; Back always returns to the page.
enum class Sheet { None, Network, Password, Hidden, Confirm, Picker };
enum class Zone { Sidebar, Content };

int section = 0;
Zone zone = Zone::Sidebar;
Sheet sheet = Sheet::None;
std::vector<Item> items;
int focus = -1;

std::string toast;
uint32_t toast_until = 0;

// Sheet state.
std::string sheet_ssid, sheet_id, password, hidden_ssid;
bool sheet_open = false, show_password = false;
int input_target = 0;  // 0 password, 1 hidden SSID
std::string confirm_title, confirm_text, confirm_button;
std::function<void()> confirm_fn;
// Picker: full-row options for a Choice, easier to hit than inline arrows.
std::string picker_label;
std::vector<std::string> picker_options;
int picker_current = 0;
std::function<void(int)> picker_fn;
std::string return_key;  // page row to refocus when a sheet closes

// Wi-Fi state machine; nothing here blocks for more than one wpa_cli call.
enum class Job { None, Scan, Connect };
Job job = Job::None;
uint32_t job_started = 0, job_poll = 0, last_scan = 0;
std::string job_id, job_ssid;
bool job_new = false, job_selected = false;
std::string job_replaced_id, job_previous_id;
std::vector<SavedNet> job_before;
bool scanned = false;
WifiStatus wifi;
std::vector<SavedNet> saved;
std::vector<ScanNet> nearby;

pid_t sshd_action_pid = -1;
std::string sshd_action_name;
uint32_t sshd_action_started = 0;

// ----------------------------------------------------------------- UI objects

lv_font_t *font = nullptr, *small = nullptr;
lv_obj_t *side_items[kSections] = {};
lv_obj_t *back_button = nullptr, *title_label = nullptr, *status_label = nullptr, *hint_label = nullptr, *list = nullptr;

constexpr uint32_t kBg = 0x0e1621, kSide = 0x141f2c, kRow = 0x1a2635, kFocus = 0x24577a, kFocusDim = 0x1f3a52;
constexpr uint32_t kText = 0xeef3f8, kSub = 0x8fa3b8, kAccent = 0x5cc8ff, kWarn = 0xf0c674, kDanger = 0xff7a7a;

void show_toast(const std::string &text, uint32_t ms = 3500) {
    toast = text;
    toast_until = screen::tick() + ms;
}

void sync_list(bool reset_focus = false);
void paint_chrome();
void open_sheet(Sheet s);
void close_sheet();

bool ensure_sshd_data() {
    const std::string terminal = apps_data_dir() + "/terminal";
    const std::string data = sshd_data_dir();
    ::mkdir(terminal.c_str(), 0700);
    ::mkdir(data.c_str(), 0700);
    struct stat st{};
    return ::stat(data.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void start_sshd_action(const char *action_name) {
    if (sshd_action_pid > 0) {
        show_toast("SSH 服务操作正在进行，请稍候");
        return;
    }
    const std::string command = apps_root_dir() + "/terminal/assets/bin/sshd";
    pid_t pid = ::fork();
    if (pid < 0) {
        show_toast("无法启动 SSH 服务操作");
        return;
    }
    if (pid == 0) {
        ::setpgid(0, 0);
        ::execl(command.c_str(), command.c_str(), action_name, static_cast<char *>(nullptr));
        _exit(127);
    }
    sshd_action_pid = pid;
    sshd_action_name = action_name;
    sshd_action_started = screen::tick();
    show_toast(std::string(action_name == std::string("start") ? "正在启动" : "正在停止") + " SSH 服务…", 30000);
    sync_list();
}

void poll_sshd_action() {
    if (sshd_action_pid <= 0) return;
    int status = 0;
    const pid_t done = ::waitpid(sshd_action_pid, &status, WNOHANG);
    if (done == 0) {
        if (screen::tick() - sshd_action_started > 30000) {
            ::kill(sshd_action_pid, SIGTERM);
            ::waitpid(sshd_action_pid, &status, 0);
            show_toast("SSH 服务操作超时，请检查终端日志", 5000);
            sshd_action_pid = -1;
            sshd_action_name.clear();
            sync_list();
        }
        return;
    }
    if (done < 0 && errno == EINTR) return;
    const bool exited = done == sshd_action_pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    const bool want_running = sshd_action_name == "start";
    const bool state_ok = sshd_running() == want_running;
    if (exited && state_ok) {
        show_toast(want_running ? "SSH 服务已启动（端口 2222）" : "SSH 服务已停止");
    } else {
        show_toast(want_running ? "SSH 服务启动失败，请检查终端日志" : "SSH 服务停止失败，请检查终端日志", 5000);
    }
    sshd_action_pid = -1;
    sshd_action_name.clear();
    sync_list();
}

void cancel_sshd_action() {
    if (sshd_action_pid <= 0) return;
    ::kill(sshd_action_pid, SIGTERM);
    while (::waitpid(sshd_action_pid, nullptr, 0) < 0 && errno == EINTR) {}
    sshd_action_pid = -1;
    sshd_action_name.clear();
}

void set_sshd_auto_start(bool enabled) {
    if (!ensure_sshd_data() || !write_file(sshd_enabled_file(), enabled ? "1\n" : "0\n")) {
        show_toast("无法保存 SSH 开机启动设置", 5000);
        return;
    }
    show_toast(enabled ? "已设置开机自动启动 SSH" : "已取消开机自动启动 SSH");
    if (enabled && !sshd_running()) start_sshd_action("start");
    sync_list();
}

// ------------------------------------------------------------ Wi-Fi actions

void start_scan() {
    if (job != Job::None) return;
    wpa({"scan"});
    job = Job::Scan;
    job_started = screen::tick();
}

// select_network disables every other profile. Restore each prior flag rather
// than enable_network all, which changes the user's saved preferences.
bool restore_network_flags(bool rollback) {
    if (!job_selected) return true;
    bool restored = true;
    if (rollback && !job_previous_id.empty())
        restored = wpa_ok({"select_network", job_previous_id}) && restored;
    for (const auto &n : job_before) {
        if (!rollback && (n.id == job_replaced_id || n.id == job_id)) continue;
        restored = wpa_ok({n.disabled ? "disable_network" : "enable_network", n.id}) && restored;
    }
    // A user who was disconnected must remain disconnected after a failed try.
    if (rollback && job_previous_id.empty()) restored = wpa_ok({"disconnect"}) && restored;
    return restored;
}

void finish_connect(bool ok, const std::string &why) {
    if (job != Job::Connect) return;
    if (ok) {
        // Only now may a tested replacement supersede the original credentials.
        bool complete = true;
        if (!job_replaced_id.empty()) complete = wpa_ok({"remove_network", job_replaced_id});
        complete = restore_network_flags(false) && complete;
        const bool persisted = complete && wpa_ok({"save_config"});
        show_toast(persisted ? "已连接 " + display_ssid(job_ssid) : "已连接，但配置保存失败，请重试", 5000);
    } else {
        bool restored = true;
        if (job_new) restored = wpa_ok({"remove_network", job_id});
        restored = restore_network_flags(true) && restored;
        if (!restored) std::fprintf(stderr, "[settings] Wi-Fi rollback incomplete\n");
        show_toast(restored ? why : why + "；网络恢复未完成，请检查 WLAN", 5000);
    }
    job = Job::None;
    job_new = job_selected = false;
    job_before.clear();
    job_replaced_id.clear();
    job_previous_id.clear();
    saved = read_saved();
    wifi = read_wifi();
}

void cancel_connect() {
    if (job == Job::Connect) finish_connect(false, "已取消连接，恢复原网络");
}

// Empty psk keeps an existing profile. A changed key is tried in a temporary
// profile; never write a guessed password over the last known working key.
void start_connect(const std::string &ssid, const std::string &existing_id, const std::string &psk, bool open) {
    if (job == Job::Connect) { show_toast("正在连接其他网络，请稍候"); return; }
    if (job == Job::Scan) job = Job::None;
    bool valid = false;
    auto before = read_saved(&valid);
    const auto previous = read_wifi();
    if (!valid || previous.state.empty()) { show_toast("无法读取原网络状态，连接已取消"); return; }
    if (!existing_id.empty() && std::none_of(before.begin(), before.end(), [&](const SavedNet &n) { return n.id == existing_id; })) {
        show_toast("已保存的网络已变化，请刷新列表"); return;
    }
    job_before = std::move(before);
    job_previous_id = previous.id;
    job_replaced_id = !existing_id.empty() && !psk.empty() ? existing_id : "";
    job_id = existing_id;
    job_ssid = ssid;
    job_new = job_selected = false;
    job = Job::Connect;
    if (existing_id.empty() || !job_replaced_id.empty()) {
        std::string out;
        if (wpa({"add_network"}, &out) != 0) { finish_connect(false, "无法新建网络（wpa_cli 无响应）"); return; }
        job_id = first_line(out);
        if (job_id.empty() || !std::all_of(job_id.begin(), job_id.end(), [](unsigned char c) { return std::isdigit(c); })) {
            finish_connect(false, "无法新建网络"); return;
        }
        job_new = true;
        bool ok = wpa_ok({"set_network", job_id, "ssid", hex_encode(ssid)});
        if (!job_replaced_id.empty()) {
            // Preserve non-secret connection constraints in an edited profile.
            for (const char *field : {"key_mgmt", "proto", "pairwise", "group", "auth_alg", "scan_ssid", "priority", "bssid", "ieee80211w"}) {
                std::string value;
                if (wpa({"get_network", existing_id, field}, &value) != 0) { ok = false; break; }
                value = first_line(value);
                if (!value.empty() && value != "FAIL") ok = wpa_ok({"set_network", job_id, field, value}) && ok;
            }
        }
        if (ok && open) ok = wpa_ok({"set_network", job_id, "key_mgmt", "NONE"});
        if (ok && hidden_ssid == ssid) ok = wpa_ok({"set_network", job_id, "scan_ssid", "1"});
        if (!ok) { finish_connect(false, "写入网络配置失败"); return; }
    }
    if (!psk.empty()) {
        const bool raw = psk.size() == 64 && std::all_of(psk.begin(), psk.end(), [](unsigned char c) { return std::isxdigit(c); });
        if (!wpa_ok({"set_network", job_id, "psk", raw ? psk : "\"" + psk + "\""})) {
            finish_connect(false, "密码写入失败"); return;
        }
    }
    // Set this before the command: even a lost response requires rollback.
    job_selected = true;
    if (!wpa_ok({"select_network", job_id})) { finish_connect(false, "无法选择网络"); return; }
    job_started = job_poll = screen::tick();
    show_toast("开始连接，请确保密码输入正确", 3000);
}

void poll_jobs() {
    const uint32_t now = screen::tick();
    if (job == Job::Scan && now - job_started >= 3500) {
        nearby = read_scan();
        saved = read_saved();
        scanned = true;
        last_scan = now;
        job = Job::None;
        if (section == static_cast<int>(Section::Wifi)) sync_list();
        return;
    }
    if (job == Job::Connect && now - job_poll >= 1000) {
        job_poll = now;
        wifi = read_wifi();
        if (wifi.state == "COMPLETED" && wifi.id == job_id && !wifi.ip.empty()) {
            finish_connect(true, "");
        } else {
            bool wrong_key = false;
            for (auto &n : read_saved())
                if (n.id == job_id && n.temp_disabled) wrong_key = true;
            if (wrong_key) finish_connect(false, "密码错误，网络连接失败");
            else if (now - job_started > 25000) finish_connect(false, "连接超时：请检查信号或密码");
        }
        sync_list();
    }
}

const SavedNet *saved_by_ssid(const std::string &ssid) {
    for (auto &n : saved) if (n.ssid == ssid) return &n;
    return nullptr;
}

void choose_nearby(const ScanNet &n) {
    if (auto *s = saved_by_ssid(n.ssid)) {
        if (s->current && wifi.state == "COMPLETED") { sheet_ssid = s->ssid; sheet_id = s->id; open_sheet(Sheet::Network); return; }
        start_connect(n.ssid, s->id, "", false);
        return;
    }
    if (n.eap) { show_toast("暂不支持企业级（802.1X）网络"); return; }
    if (n.wep) { show_toast("暂不支持 WEP 加密网络"); return; }
    if (!n.secure) { start_connect(n.ssid, "", "", true); return; }
    sheet_ssid = n.ssid;
    sheet_id.clear();
    open_sheet(Sheet::Password);
}

void submit_password() {
    const bool editing = !sheet_id.empty();
    const std::string ssid = sheet == Sheet::Hidden ? hidden_ssid : sheet_ssid;
    if (sheet == Sheet::Hidden && ssid.empty()) { show_toast("请输入网络名称"); input_target = 1; sync_list(); return; }
    if (sheet == Sheet::Hidden && ssid.size() > 32) { show_toast("网络名称最长 32 字节"); return; }
    const bool open = sheet == Sheet::Hidden && password.empty();
    if (!open && !(editing && password.empty()) && (password.size() < 8 || password.size() > 64)) {
        show_toast("密码长度需在 8 到 63 个字符");
        return;
    }
    std::string psk = password, id = sheet_id;
    if (sheet == Sheet::Hidden) {
        if (auto *s = saved_by_ssid(ssid)) id = s->id;
    } else {
        hidden_ssid.clear();
    }
    close_sheet();
    start_connect(ssid, id, psk, open);
    std::fill(psk.begin(), psk.end(), '\0');
    sync_list();
}

// ----------------------------------------------------------------- pages

std::vector<Item> wifi_page() {
    std::vector<Item> v;
    std::string state;
    if (job == Job::Connect) state = "正在连接 " + display_ssid(job_ssid) + "…";
    else if (wifi.state == "COMPLETED") state = "已连接";
    else if (wifi.state == "DISCONNECTED" || wifi.state == "INACTIVE") state = "未连接";
    else if (wifi.state.empty()) state = "WLAN 不可用";
    else state = "连接中（" + wifi.state + "）";
    v.push_back(info("状态", state));
    if (wifi.state == "COMPLETED") {
        v.push_back(info("当前网络", display_ssid(wifi.ssid)));
        v.push_back(info("IP 地址", wifi.ip.empty() ? "正在获取…" : wifi.ip));
        if (wifi.rssi) v.push_back(info("信号", format("%s  %d dBm", signal_word(wifi.rssi), wifi.rssi)));
    }
    v.push_back(action("scan", "刷新网络列表", job == Job::Scan ? "扫描中…" : scanned ? format("%zu 个", nearby.size()) : "",
                       [] { start_scan(); sync_list(); }));
    v.push_back(action("hidden", "添加其他网络", LV_SYMBOL_RIGHT, [] {
        hidden_ssid.clear(); sheet_id.clear(); open_sheet(Sheet::Hidden);
    }));
    if (!saved.empty()) {
        v.push_back(header("已保存的网络"));
        for (auto &n : saved) {
            std::string val = n.current && wifi.state == "COMPLETED" ? "已连接" : n.temp_disabled ? "密码错误" : n.disabled ? "已停用" : "";
            auto item = action("saved:" + n.id, display_ssid(n.ssid), val + (val.empty() ? "" : "  ") + LV_SYMBOL_RIGHT,
                               [id = n.id, ssid = n.ssid] { sheet_id = id; sheet_ssid = ssid; open_sheet(Sheet::Network); });
            item.accent = n.current && wifi.state == "COMPLETED";
            v.push_back(std::move(item));
        }
    }
    v.push_back(header(job == Job::Scan && nearby.empty() ? "附近的网络（正在搜索WIFI网络…）" : "附近的网络"));
    if (scanned && nearby.empty()) v.push_back(note("none", "未找到网络。"));
    for (auto &n : nearby) {
        std::string val = signal_word(n.level);
        val += n.secure ? " · 加密" : " · 开放";
        if (saved_by_ssid(n.ssid)) val = "已保存 · " + val;
        v.push_back(action("scan:" + n.ssid, display_ssid(n.ssid), val, [n] { choose_nearby(n); }));
    }
    return v;
}

std::vector<Item> network_sheet() {
    std::vector<Item> v;
    if (job == Job::Connect) {
        v.push_back(info("正在连接", display_ssid(job_ssid)));
        v.push_back(action("cancel-connect", "取消连接", "恢复原网络", [] { cancel_connect(); close_sheet(); sync_list(); }));
        return v;
    }
    const bool connected = wifi.state == "COMPLETED" && wifi.id == sheet_id;
    v.push_back(info("网络", display_ssid(sheet_ssid)));
    if (connected) {
        v.push_back(info("IP 地址", wifi.ip.empty() ? "正在获取…" : wifi.ip));
        if (wifi.rssi) v.push_back(info("信号", format("%s  %d dBm", signal_word(wifi.rssi), wifi.rssi)));
        v.push_back(action("disconnect", "断开连接", "", [] {
            wpa({"disconnect"}); close_sheet(); wifi = read_wifi(); show_toast("已断开"); sync_list();
        }));
    } else {
        v.push_back(action("connect", "连接", "", [] {
            std::string id = sheet_id, ssid = sheet_ssid; close_sheet(); start_connect(ssid, id, "", false); sync_list();
        }));
    }
    v.push_back(action("repass", "修改密码", LV_SYMBOL_RIGHT, [] { open_sheet(Sheet::Password); }));
    auto forget = action("forget", "忘记此网络", "", [] {
        confirm_title = "忘记网络";
        confirm_text = "将删除「" + display_ssid(sheet_ssid) + "」的已保存密码。";
        confirm_button = "忘记";
        confirm_fn = [] {
            wpa({"remove_network", sheet_id});
            wpa({"save_config"});
            show_toast("已忘记 " + display_ssid(sheet_ssid));
            saved = read_saved();
            wifi = read_wifi();
        };
        open_sheet(Sheet::Confirm);
    });
    forget.danger = true;
    v.push_back(std::move(forget));
    return v;
}

std::string masked(const std::string &s, bool show, bool caret) {
    std::string out;
    if (show) out = s;
    else for (size_t i = 0; i < s.size(); ++i) out += "•";
    if (caret) out += "|";
    return out;
}

std::vector<Item> password_sheet() {
    std::vector<Item> v;
    const bool hidden = sheet == Sheet::Hidden;
    if (hidden) {
        Item name;
        name.kind = Kind::Input;
        name.key = "in:ssid";
        name.label = "网络名称";
        name.value = hidden_ssid + (input_target == 1 ? "|" : "");
        if (hidden_ssid.empty() && input_target != 1) name.value = "未设置";
        name.on_enter = [] { input_target = 0; sync_list(); };
        v.push_back(std::move(name));
    } else {
        v.push_back(info("网络", display_ssid(sheet_ssid)));
    }
    Item pw;
    pw.kind = Kind::Input;
    pw.key = "in:pw";
    pw.label = "密码";
    pw.value = masked(password, show_password, input_target == 0);
    if (password.empty() && input_target != 0) pw.value = hidden ? "开放网络可留空" : "未设置";
    pw.on_enter = [] { submit_password(); };
    v.push_back(std::move(pw));
    auto toggle = action("show", "显示密码（拍照键）", show_password ? "开" : "关", [] { show_password = !show_password; sync_list(); });
    v.push_back(std::move(toggle));
    auto go = action("go", sheet_id.empty() ? "连接" : "保存并连接", "", [] { submit_password(); });
    go.accent = true;
    v.push_back(std::move(go));
    v.push_back(action("cancel", "取消", "", [] { close_sheet(); }));
    std::string tip = !sheet_id.empty() ? "留空则沿用已保存的密码。" : hidden ? "输入名称后按回车再输入密码。" : "";
    tip += format("Shift+字母输入数字和符号，双击 Shift 切换大写（当前%s）。", screen::caps_lock() ? "大写" : "小写");
    v.push_back(note("tip", tip));
    return v;
}

std::vector<Item> confirm_sheet() {
    std::vector<Item> v;
    v.push_back(note("text", confirm_text));
    auto ok = action("ok", confirm_button, "", [] {
        auto fn = confirm_fn;
        close_sheet();
        if (fn) fn();
        sync_list();
    });
    ok.danger = true;
    v.push_back(std::move(ok));
    v.push_back(action("cancel", "取消", "", [] { close_sheet(); }));
    return v;
}

struct TimerOption { const char *label; long ms; };
const TimerOption kScreenOff[] = {{"30 秒", 30000}, {"1 分钟", 60000}, {"2 分钟", 120000}, {"5 分钟", 300000},
                                  {"10 分钟", 600000}, {"20 分钟", 1200000}, {"30 分钟", 1800000}, {"永不", 0}};
constexpr int kScreenOffCount = sizeof kScreenOff / sizeof kScreenOff[0];

std::vector<Item> display_page() {
    std::vector<Item> v;
    Item bright;
    bright.kind = Kind::Slider;
    bright.key = "s:bright";
    bright.label = "屏幕亮度";
    bright.lo = 1;
    bright.hi = backlight_max();
    bright.cur = std::clamp(backlight_level(), bright.lo, bright.hi);
    bright.value = format("%d / %d", bright.cur, bright.hi);
    bright.on_change = [](int value) { set_backlight(value); };
    v.push_back(std::move(bright));

    Item off;
    off.kind = Kind::Choice;
    off.key = "c:screenoff";
    off.label = "自动熄屏";
    for (auto &o : kScreenOff) off.options.push_back(o.label);
    // The persisted preference wins over the live (volatile) properties;
    // only when the file is absent do we infer the choice from them.
    const std::string saved = read_file((data_dir() + "/screenoff").c_str());
    const bool never = !saved.empty() ? value_after(saved, "lock") == "1"
                                      : prop("sys.backlight.lock") == "1";
    const long ms = !saved.empty() ? to_long(value_after(saved, "timer"), 1200000)
                                   : to_long(prop("sys.backlight.timer"), 1200000);
    off.option = kScreenOffCount - 1;
    if (!never) {
        int best = 0;
        for (int i = 0; i < kScreenOffCount - 1; ++i)
            if (std::labs(kScreenOff[i].ms - ms) < std::labs(kScreenOff[best].ms - ms)) best = i;
        off.option = best;
    }
    off.on_change = [](int index) {
        const auto &o = kScreenOff[index];
        if (o.ms) set_prop("sys.backlight.timer", std::to_string(o.ms));
        set_prop("sys.backlight.lock", o.ms ? "0" : "1");
        set_prop("sys.backlight.timer.reset", "1");
        // Persist the choice; desktop-service.sh reapplies it at boot.
        ::mkdir(data_dir().c_str(), 0700);
        write_file(data_dir() + "/screenoff", format("lock=%d\ntimer=%ld\n", o.ms ? 0 : 1, o.ms));
    };
    v.push_back(std::move(off));

    v.push_back(header("休眠"));
    const std::string lock = prop("sys.suspend.lock");
    v.push_back(info("熄屏后休眠", duration_text(to_long(prop("sys.suspend.timer"), 0))));
    v.push_back(info("休眠锁", lock == "1" ? "已锁定（不进入休眠）" : lock.empty() ? "未知" : "未锁定"));
    v.push_back(note("tip", "A/D 直接调节。亮度会同步给原厂电源管理，熄屏唤醒后保持。休眠由原厂电源管理控制，这里只显示。"));
    return v;
}

std::vector<Item> sound_page() {
    std::vector<Item> v;
    Item vol;
    vol.kind = Kind::Slider;
    vol.key = "s:volume";
    vol.label = "媒体音量";
    vol.lo = 0;
    vol.hi = 100;
    vol.step = 5;
    vol.cur = std::max(0, audio.percent());
    vol.value = audio.ctl ? format("%d%%", vol.cur) : "不可用";
    vol.on_change = [](int value) { audio.set_percent(value); };
    v.push_back(std::move(vol));
    std::string jack = sysfs("/sys/devices/virtual/switch/h2w/state");
    v.push_back(info("输出", jack.empty() || jack == "0" ? "扬声器" : "耳机"));
    v.push_back(note("tip", "侧边音量键同样可以调节。回到原厂桌面时会恢复进入应用前的音量。"));
    return v;
}

std::string usb_mode_name(const std::string &s) {
    if (s == "adb") return "ADB 调试";
    if (s == "mtp") return "文件传输（MTP）";
    if (s == "mtp,adb" || s == "adb,mtp") return "MTP + ADB";
    if (s.empty() || s == "none") return "无";
    return s;
}

void switch_usb(const char *mode) {
    set_prop("user.usb.config", mode);
    show_toast(std::string("已请求切换到 ") + usb_mode_name(mode));
}

std::vector<Item> usb_page() {
    std::vector<Item> v;
    const std::string state = prop("sys.usb.state"), want = prop("user.usb.config");
    v.push_back(info("当前模式", usb_mode_name(state)));
    if (!want.empty() && want != state) v.push_back(info("正在切换到", usb_mode_name(want)));
    v.push_back(info("USB 线", sysfs("/sys/class/power_supply/usb/online") == "1" ? "已连接" : "未连接"));
    v.push_back(header("切换模式"));
    auto adb = action("adb", "ADB 调试", state == "adb" ? "当前" : "", [] { switch_usb("adb"); });
    adb.accent = state == "adb";
    v.push_back(std::move(adb));
    auto mtp = action("mtp", "文件传输（MTP）", state == "mtp" ? "当前" : "", [] {
        confirm_title = "切换到文件传输";
        confirm_text = "电脑将能浏览本机存储，但 USB 上的 ADB 调试会断开。之后可以回到这里切回 ADB。";
        confirm_button = "切换到 MTP";
        confirm_fn = [] { switch_usb("mtp"); };
        open_sheet(Sheet::Confirm);
    });
    mtp.accent = state == "mtp";
    v.push_back(std::move(mtp));
    return v;
}

std::vector<Item> ssh_page() {
    std::vector<Item> v;
    const bool on = sshd_running();
    const bool busy = sshd_action_pid > 0;
    const bool auto_start = sshd_auto_start();
    const std::string service_label = busy ? "SSH 服务操作" : on ? "停止 SSH 服务" : "启动 SSH 服务";
    auto service = action("service", service_label,
                          busy ? (sshd_action_name == "start" ? "正在启动" : "正在停止") : on ? "正在运行" : "已停止",
                          [on, busy] {
                              if (busy) show_toast("SSH 服务操作正在进行，请稍候");
                              else start_sshd_action(on ? "stop" : "start");
                          });
    service.accent = !on && !busy;
    service.danger = on && !busy;
    v.push_back(std::move(service));
    v.push_back(info("端口", "2222"));
    v.push_back(info("认证", "仅公钥"));
    if (on) {
        const std::string ip = wifi.ip.empty() ? read_wifi().ip : wifi.ip;
        v.push_back(info("电脑上运行", ip.empty() ? "未连接 WLAN" : "ssh -p 2222 root@" + ip));
    }
    v.push_back(action("autostart", "开机自动启动", auto_start ? "已开启" : "未开启", [auto_start] {
                           set_sshd_auto_start(!auto_start);
                       }));
    v.push_back(note("tip", "公钥文件：/storage/terminal/dropbear/authorized_keys。SSH 服务不依赖网络 ADB；首次启动会生成主机密钥。"));
    return v;
}

std::vector<Item> battery_page() {
    std::vector<Item> v;
    const char *b = "/sys/class/power_supply/battery/";
    auto f = [&](const char *name) { return sysfs((std::string(b) + name).c_str()); };
    Item level;
    level.kind = Kind::Meter;
    level.key = "m:level";
    level.label = "电量";
    level.cur = static_cast<int>(to_long(f("capacity"), 0));
    level.value = f("capacity").empty() ? "未知" : format("%d%%", level.cur);
    v.push_back(std::move(level));
    v.push_back(info("状态", battery_status_text(f("status"))));
    v.push_back(info("充电器", sysfs("/sys/class/power_supply/usb/online") == "1" ? "已接入" : "未接入"));
    long uv = to_long(f("voltage_now"), 0);
    if (uv > 0) v.push_back(info("电压", format("%.2f V", uv / 1e6)));
    long t = to_long(f("temp"), -1000);
    if (t > -1000) v.push_back(info("温度", format("%.0f ℃", t > 200 ? t / 10.0 : static_cast<double>(t))));
    std::string health = f("health");
    if (!health.empty()) v.push_back(info("健康", health == "Good" ? "良好" : health));
    std::string tech = f("technology");
    v.push_back(info("电池", tech.empty() ? "锂电池" : tech));
    v.push_back(note("tip", "电量低于 5% 时原厂电源管理会自动关机，请及时充电。"));
    return v;
}

std::string mem_text() {
    std::string mem = read_file("/proc/meminfo", 4096);
    auto field = [&](const char *key) {
        auto at = mem.find(key);
        return at == std::string::npos ? -1L : std::strtol(mem.c_str() + at + std::strlen(key), nullptr, 10);
    };
    long total = field("MemTotal:"), avail = field("MemAvailable:");
    if (total < 0) return "未知";
    return format("可用 %ld MB / 共 %ld MB", avail / 1024, total / 1024);
}

std::string uptime_text() {
    long s = static_cast<long>(std::strtod(read_file("/proc/uptime", 64).c_str(), nullptr));
    long d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
    return d ? format("%ld 天 %ld 小时 %ld 分", d, h, m) : format("%ld 小时 %ld 分", h, m);
}

std::string os_release(const char *key) {
    std::istringstream in(read_file("/etc/os-release", 4096));
    std::string line, prefix = std::string(key) + "=";
    while (std::getline(in, line)) {
        if (line.compare(0, prefix.size(), prefix) != 0) continue;
        std::string value = line.substr(prefix.size());
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        return value;
    }
    return "未知";
}

std::vector<Item> about_page() {
    std::vector<Item> v;
    std::string cpu = read_file("/proc/cpuinfo", 4096);
    auto field = [&](const char *key) {
        auto at = cpu.find(key);
        if (at == std::string::npos) return std::string("未知");
        auto end = cpu.find('\n', at);
        auto line = cpu.substr(at, end == std::string::npos ? std::string::npos : end - at);
        auto colon = line.find(':');
        return colon == std::string::npos ? std::string("未知") : trim(line.substr(colon + 1));
    };
    std::string kernel = first_line(read_file("/proc/version", 240));
    if (kernel.compare(0, 14, "Linux version ") == 0) kernel = "Linux " + kernel.substr(14, kernel.find(' ', 14) - 14);
    v.push_back(info("产品型号", "快易典 C1 Max"));
    v.push_back(info("处理器", "Ingenic X2000（" + field("system type") + "）"));
    v.push_back(info("内存", mem_text()));
    v.push_back(info("存储空间", storage_text("/storage")));
    v.push_back(info("系统分区", storage_text("/")));
    v.push_back(info("屏幕", "800 × 340 触摸屏"));
    v.push_back(header("软件"));
    v.push_back(info("系统版本", os_release("PRETTY_NAME")));
    v.push_back(info("内核", kernel));
    v.push_back(info("设置", kVersion));
    v.push_back(info("已运行", uptime_text()));
    v.push_back(header("标识"));
    v.push_back(info("WLAN 地址", sysfs("/sys/class/net/wlan0/address")));
    std::string bt = prop("bluetooth.name");
    if (!bt.empty()) v.push_back(info("蓝牙名称", bt));
    std::string serial = sysfs("/sys/kernel/config/usb_gadget/demo/strings/0x409/serialnumber");
    v.push_back(info("序列号", serial.empty() ? "未知" : serial));
    return v;
}

std::vector<Item> picker_sheet() {
    std::vector<Item> v;
    for (int i = 0; i < static_cast<int>(picker_options.size()); ++i) {
        auto item = action("opt:" + std::to_string(i), picker_options[i], i == picker_current ? LV_SYMBOL_OK : "", [i] {
            auto fn = picker_fn;
            const std::string text = picker_label + "：" + picker_options[i];
            close_sheet();
            if (fn) fn(i);
            show_toast(text, 1500);
            sync_list();
        });
        item.accent = i == picker_current;
        v.push_back(std::move(item));
    }
    return v;
}

std::vector<Item> build_items() {
    switch (sheet) {
    case Sheet::Network: return network_sheet();
    case Sheet::Password:
    case Sheet::Hidden: return password_sheet();
    case Sheet::Confirm: return confirm_sheet();
    case Sheet::Picker: return picker_sheet();
    case Sheet::None: break;
    }
    switch (static_cast<Section>(section)) {
    case Section::Wifi: return wifi_page();
    case Section::Display: return display_page();
    case Section::Sound: return sound_page();
    case Section::Usb: return usb_page();
    case Section::Ssh: return ssh_page();
    case Section::Battery: return battery_page();
    case Section::About: return about_page();
    }
    return {};
}

std::string sheet_title() {
    switch (sheet) {
    case Sheet::Network: return display_ssid(sheet_ssid);
    case Sheet::Password: return sheet_id.empty() ? "输入 Wi-Fi 密码" : "修改密码";
    case Sheet::Hidden: return "添加其他网络";
    case Sheet::Confirm: return confirm_title;
    case Sheet::Picker: return picker_label;
    case Sheet::None: break;
    }
    return kSectionName[section];
}

// ------------------------------------------------------------ list widgets

void set_text(lv_obj_t *label, const std::string &text) {
    if (!label) return;
    const char *now = lv_label_get_text(label);
    if (!now || text != now) lv_label_set_text(label, text.c_str());
}

std::string choice_text(const Item &it) {
    return it.options[it.option] + "  " LV_SYMBOL_RIGHT;
}

void restyle(int index) {
    auto &it = items[index];
    if (!it.row || !it.focusable()) return;
    const bool focused = index == focus && zone == Zone::Content;
    lv_obj_set_style_bg_color(it.row, lv_color_hex(focused ? kFocus : kRow), 0);
    lv_obj_set_style_border_width(it.row, focused ? 2 : 0, 0);
    uint32_t name_color = it.danger ? kDanger : it.accent ? kAccent : kText;
    lv_obj_set_style_text_color(it.name, lv_color_hex(name_color), 0);
}

void apply_value(Item &it) {
    switch (it.kind) {
    case Kind::Choice: set_text(it.val, choice_text(it)); break;
    case Kind::Slider:
    case Kind::Meter:
        set_text(it.val, it.value);
        if (it.bar) lv_bar_set_value(it.bar, it.cur, LV_ANIM_OFF);
        break;
    default: set_text(it.val, it.value); break;
    }
}

void item_clicked(lv_event_t *e);
void adjust(int dir);
void set_focus(int index, bool anim = true);

void step_clicked(lv_event_t *e) {
    const intptr_t packed = reinterpret_cast<intptr_t>(lv_event_get_user_data(e));
    const int index = static_cast<int>(packed >> 1), dir = (packed & 1) ? 1 : -1;
    if (index < 0 || index >= static_cast<int>(items.size())) return;
    zone = Zone::Content;
    if (focus != index) set_focus(index);
    adjust(dir);
}

void make_step_button(lv_obj_t *row, int index, int dir, int x) {
    auto *b = lv_obj_create(row);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(b, 50, 40);
    lv_obj_set_style_radius(b, 20, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_set_style_pad_all(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x2c3b4e), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(kAccent), LV_STATE_PRESSED);
    lv_obj_align(b, LV_ALIGN_RIGHT_MID, x, 0);
    lv_obj_add_event_cb(b, step_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>((static_cast<intptr_t>(index) << 1) | (dir > 0)));
    auto *t = lv_label_create(b);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_18, 0);
    lv_label_set_text(t, dir > 0 ? LV_SYMBOL_PLUS : LV_SYMBOL_MINUS);
    lv_obj_center(t);
}

void create_row(int index) {
    auto &it = items[index];
    auto *row = lv_obj_create(list);
    it.row = row;
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_hor(row, 14, 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(kAccent), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    if (it.kind == Kind::Header || it.kind == Kind::Note) {
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
        it.name = lv_label_create(row);
        lv_obj_set_style_text_font(it.name, small, 0);
        lv_obj_set_style_text_color(it.name, lv_color_hex(it.kind == Kind::Header ? kAccent : kSub), 0);
        lv_obj_set_width(it.name, lv_pct(100));
        lv_label_set_long_mode(it.name, LV_LABEL_LONG_WRAP);
        lv_label_set_text(it.name, it.label.c_str());
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_top(row, it.kind == Kind::Header ? 8 : 4, 0);
        lv_obj_set_style_pad_bottom(row, 2, 0);
        return;
    }
    lv_obj_set_height(row, 48);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(0x31506e), LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, item_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(index)));
    it.name = lv_label_create(row);
    lv_label_set_text(it.name, it.label.c_str());
    lv_label_set_long_mode(it.name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(it.name, it.kind == Kind::Action && it.value.empty() ? 520 : 220);
    lv_obj_set_height(it.name, lv_font_get_line_height(font));
    lv_obj_align(it.name, LV_ALIGN_LEFT_MID, 0, 0);
    it.val = lv_label_create(row);
    lv_obj_set_style_text_color(it.val, lv_color_hex(it.kind == Kind::Input ? kText : kSub), 0);
    lv_label_set_long_mode(it.val, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(it.val, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_height(it.val, lv_font_get_line_height(font));
    if (it.kind == Kind::Slider || it.kind == Kind::Meter) {
        it.bar = lv_bar_create(row);
        lv_bar_set_range(it.bar, it.lo, it.hi);
        lv_obj_set_style_bg_color(it.bar, lv_color_hex(0x2c3b4e), 0);
        lv_obj_set_style_bg_color(it.bar, lv_color_hex(kAccent), LV_PART_INDICATOR);
        lv_obj_remove_flag(it.bar, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_width(it.val, 72);
        if (it.kind == Kind::Slider) {
            // Large -/+ targets: the thin bar alone is hard to hit on this panel.
            lv_obj_set_size(it.bar, 150, 12);
            lv_obj_align(it.bar, LV_ALIGN_RIGHT_MID, -134, 0);
            make_step_button(row, index, -1, -292);
            make_step_button(row, index, 1, -78);
        } else {
            lv_obj_set_size(it.bar, 190, 10);
            lv_obj_align(it.bar, LV_ALIGN_RIGHT_MID, -86, 0);
        }
    } else if (it.kind == Kind::Input) {
        lv_obj_set_width(it.val, 320);
    } else {
        lv_obj_set_width(it.val, it.kind == Kind::Action && it.value.empty() ? 20 : 320);
    }
    lv_obj_align(it.val, LV_ALIGN_RIGHT_MID, 0, 0);
    apply_value(it);
    restyle(index);
}

void scroll_to_focus(bool anim) {
    if (focus < 0 || focus >= static_cast<int>(items.size())) return;
    int first = -1, last = -1;
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (items[i].focusable()) { if (first < 0) first = i; last = i; }
    const auto mode = anim ? LV_ANIM_ON : LV_ANIM_OFF;
    if (focus == first) lv_obj_scroll_to_y(list, 0, mode);
    else if (focus == last) lv_obj_scroll_to_view(items.back().row, mode);
    else {
        // Keep the header above the focused row visible too.
        if (focus > 0 && items[focus - 1].kind == Kind::Header) lv_obj_scroll_to_view(items[focus - 1].row, mode);
        lv_obj_scroll_to_view(items[focus].row, mode);
    }
}

int first_focusable() {
    for (int i = 0; i < static_cast<int>(items.size()); ++i) if (items[i].focusable()) return i;
    return -1;
}

void set_focus(int index, bool anim) {
    int old = focus;
    focus = index;
    if (old >= 0 && old < static_cast<int>(items.size())) restyle(old);
    if (focus >= 0 && focus < static_cast<int>(items.size())) restyle(focus);
    scroll_to_focus(anim);
    paint_chrome();
}

// Rebuild only when the structure changed; otherwise patch values in place so
// periodic refreshes never move focus or scroll position.
void sync_list(bool reset_focus) {
    auto next = build_items();
    bool same = !reset_focus && next.size() == items.size();
    for (size_t i = 0; same && i < next.size(); ++i)
        same = next[i].kind == items[i].kind && next[i].key == items[i].key && next[i].label == items[i].label;
    if (same) {
        for (size_t i = 0; i < next.size(); ++i) {
            auto &it = items[i];
            it.value = next[i].value;
            it.cur = next[i].cur;
            it.option = next[i].option;
            it.accent = next[i].accent;
            it.on_enter = std::move(next[i].on_enter);
            it.on_change = std::move(next[i].on_change);
            apply_value(it);
            restyle(static_cast<int>(i));
        }
        paint_chrome();
        return;
    }
    std::string focus_key = !reset_focus && focus >= 0 && focus < static_cast<int>(items.size()) ? items[focus].key : "";
    const int old_focus = focus;
    const int32_t old_scroll = lv_obj_get_scroll_y(list);
    lv_obj_clean(list);
    items = std::move(next);
    focus = -1;  // rows are styled at creation; no stale highlight
    for (int i = 0; i < static_cast<int>(items.size()); ++i) create_row(i);
    lv_obj_update_layout(list);
    focus = -1;
    for (int i = 0; !focus_key.empty() && i < static_cast<int>(items.size()); ++i)
        if (items[i].key == focus_key) focus = i;
    if (focus < 0 && !reset_focus && old_focus >= 0) {
        focus = std::min(old_focus, static_cast<int>(items.size()) - 1);
        while (focus >= 0 && !items[focus].focusable()) --focus;
    }
    if (focus < 0) focus = first_focusable();
    if (reset_focus) lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    else lv_obj_scroll_to_y(list, old_scroll, LV_ANIM_OFF);
    set_focus(focus, false);
}

void open_sheet(Sheet s) {
    if (s == Sheet::Password || s == Sheet::Hidden) {
        password.clear();
        show_password = false;
        input_target = s == Sheet::Hidden ? 1 : 0;
    }
    if (sheet == Sheet::None) return_key = focus >= 0 && focus < static_cast<int>(items.size()) ? items[focus].key : "";
    sheet = s;
    zone = Zone::Content;
    sync_list(true);
    // Start on the field to type into; confirmations default to the safe choice.
    const char *start = s == Sheet::Confirm ? "a:cancel" : s == Sheet::Hidden ? "in:ssid" : s == Sheet::Password ? "in:pw" : "";
    for (int i = 0; *start && i < static_cast<int>(items.size()); ++i)
        if (items[i].key == start) set_focus(i, false);
}

void close_sheet() {
    std::fill(password.begin(), password.end(), '\0');
    password.clear();
    const bool from_password = sheet == Sheet::Password && !sheet_id.empty();
    sheet = from_password ? Sheet::Network : Sheet::None;
    if (sheet == Sheet::None) { confirm_fn = nullptr; picker_fn = nullptr; }
    sync_list(true);
    if (sheet == Sheet::None)
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (items[i].key == return_key) set_focus(i, false);
}

void open_picker(const Item &it) {
    picker_label = it.label;
    picker_options = it.options;
    picker_current = it.option;
    picker_fn = it.on_change;
    open_sheet(Sheet::Picker);
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (items[i].key == "a:opt:" + std::to_string(picker_current)) set_focus(i, false);
}

// ------------------------------------------------------------- chrome

std::string battery_icon(int pct) {
    if (pct >= 90) return LV_SYMBOL_BATTERY_FULL;
    if (pct >= 65) return LV_SYMBOL_BATTERY_3;
    if (pct >= 40) return LV_SYMBOL_BATTERY_2;
    if (pct >= 15) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

void paint_status() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    char clock[16];
    std::strftime(clock, sizeof clock, "%H:%M", &tm);
    std::string cap = sysfs("/sys/class/power_supply/battery/capacity");
    std::string st = sysfs("/sys/class/power_supply/battery/status");
    std::string text;
    if (wifi.state == "COMPLETED") text += LV_SYMBOL_WIFI "   ";
    if (st == "Charging") text += LV_SYMBOL_CHARGE " ";
    if (!cap.empty()) text += battery_icon(static_cast<int>(to_long(cap, 0))) + " " + cap + "%   ";
    text += clock;
    set_text(status_label, text);
}

void paint_chrome() {
    for (int i = 0; i < kSections; ++i) {
        const bool selected = i == section;
        const bool active = selected && zone == Zone::Sidebar && sheet == Sheet::None;
        lv_obj_set_style_bg_color(side_items[i], lv_color_hex(active ? kFocus : selected ? kFocusDim : kSide), 0);
        lv_obj_set_style_bg_opa(side_items[i], selected ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_text_color(side_items[i], lv_color_hex(selected ? kText : kSub), 0);
    }
    const bool nested = sheet != Sheet::None;
    if (nested) lv_obj_remove_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_x(title_label, nested ? 316 : 214);
    lv_obj_set_width(title_label, nested ? 228 : 330);
    set_text(title_label, sheet_title());

    std::string hint;
    const Item *it = focus >= 0 && focus < static_cast<int>(items.size()) ? &items[focus] : nullptr;
    if (toast_until && screen::tick() < toast_until) {
        hint = toast;
        lv_obj_set_style_text_color(hint_label, lv_color_hex(kWarn), 0);
    } else {
        toast_until = 0;
        lv_obj_set_style_text_color(hint_label, lv_color_hex(kSub), 0);
        if (zone == Zone::Sidebar) hint = "W/S 选择分类   D/回车 进入   返回 退出设置";
        else if (it && it->kind == Kind::Input) hint = "直接输入   退格 删除   拍照键 显示密码   回车 确认   返回 取消";
        else if (it && (it->kind == Kind::Slider || it->kind == Kind::Choice)) hint = "A/D 调节   W/S 选择   返回 分类栏";
        else if (sheet != Sheet::None) hint = "W/S 选择   回车 确认   返回 取消";
        else hint = "W/S 选择   回车 确认   A/返回 分类栏";
    }
    set_text(hint_label, hint);
    paint_status();
}

// ---------------------------------------------------------------- input

void adjust(int dir) {
    if (focus < 0) return;
    auto &it = items[focus];
    if (it.kind == Kind::Slider) {
        int next = std::clamp(it.cur + dir * it.step, it.lo, it.hi);
        if (next == it.cur) return;
        if (it.on_change) it.on_change(next);
        sync_list();
    } else if (it.kind == Kind::Choice) {
        const int n = static_cast<int>(it.options.size());
        int next = std::clamp(it.option + dir, 0, n - 1);
        if (next == it.option) return;
        if (it.on_change) it.on_change(next);
        it.option = next;
        apply_value(it);
        show_toast(it.label + "：" + it.options[next], 1500);
        paint_chrome();
    }
}

void activate() {
    if (focus < 0) return;
    auto &it = items[focus];
    if (it.kind == Kind::Choice) { open_picker(it); return; }
    if (it.kind == Kind::Input && sheet == Sheet::Hidden && it.key == "in:ssid") {
        input_target = 0;
        sync_list();
        for (int i = 0; i < static_cast<int>(items.size()); ++i) if (items[i].key == "in:pw") set_focus(i);
        return;
    }
    if (it.on_enter) {
        auto fn = it.on_enter;
        fn();
    }
}

void move_focus(int dir) {
    if (zone == Zone::Sidebar) {
        int next = std::clamp(section + dir, 0, kSections - 1);
        if (next == section) return;
        section = next;
        sync_list(true);
        if (section == static_cast<int>(Section::Wifi) && job == Job::None && screen::tick() - last_scan > 20000) start_scan();
        paint_chrome();
        return;
    }
    for (int i = focus + dir; i >= 0 && i < static_cast<int>(items.size()); i += dir) {
        if (!items[i].focusable()) continue;
        if (sheet == Sheet::Password || sheet == Sheet::Hidden) {
            if (items[i].key == "in:pw") input_target = 0;
            else if (items[i].key == "in:ssid") input_target = 1;
            else input_target = -1;
            if (items[i].kind == Kind::Input || items[focus].kind == Kind::Input) sync_list();
        }
        set_focus(i);
        return;
    }
    // At the ends, still reveal notes above/below the first/last row.
    scroll_to_focus(true);
}

void enter_content() {
    if (first_focusable() < 0) return;
    zone = Zone::Content;
    if (focus < 0) focus = first_focusable();
    set_focus(focus, true);
}

void go_back() {
    if (sheet != Sheet::None) { close_sheet(); return; }
    if (zone == Zone::Content) { zone = Zone::Sidebar; set_focus(focus, false); return; }
    screen::quit = true;
}

bool typing() {
    return (sheet == Sheet::Password || sheet == Sheet::Hidden) && focus >= 0 && items[focus].kind == Kind::Input;
}

void physical_key(uint32_t code) {
    if (code == screen::KEY_HOME || code == screen::KEY_HOME_LONG) { screen::quit = true; return; }
    if (code == screen::KEY_EXIT) { go_back(); return; }
    if (code == screen::KEY_SYMBOL && (sheet == Sheet::Password || sheet == Sheet::Hidden)) {
        show_password = !show_password;
        sync_list();
        return;
    }
    if (code == screen::KEY_MODE) { if (sheet == Sheet::Password || sheet == Sheet::Hidden) sync_list(); return; }
    const bool enter = code == LV_KEY_ENTER || code == '\r';
    if (typing() && !enter) {
        std::string &target = input_target == 1 ? hidden_ssid : password;
        const size_t limit = input_target == 1 ? 32 : 64;
        if (code == LV_KEY_BACKSPACE || code == 8) {
            if (!target.empty()) target.pop_back();
            sync_list();
            return;
        }
        if (code >= 32 && code < 127) {
            if (target.size() < limit) target.push_back(static_cast<char>(code));
            sync_list();
            return;
        }
    }
    // The keypad has no arrows: W/S move, A/D adjust (LV arrow codes kept for USB keyboards).
    if (code == LV_KEY_UP || code == 'w' || code == 'W') { move_focus(-1); return; }
    if (code == LV_KEY_DOWN || code == 's' || code == 'S') { move_focus(1); return; }
    if (code == LV_KEY_LEFT || code == 'a' || code == 'A') {
        if (zone == Zone::Content && focus >= 0 && (items[focus].kind == Kind::Slider || items[focus].kind == Kind::Choice)) adjust(-1);
        else if (zone == Zone::Content && sheet == Sheet::None) go_back();
        return;
    }
    if (code == LV_KEY_RIGHT || code == 'd' || code == 'D') {
        if (zone == Zone::Sidebar) enter_content();
        else if (focus >= 0 && (items[focus].kind == Kind::Slider || items[focus].kind == Kind::Choice)) adjust(1);
        return;
    }
    if (enter || code == ' ') {
        if (zone == Zone::Sidebar) enter_content();
        else activate();
    }
}

void item_clicked(lv_event_t *e) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (index < 0 || index >= static_cast<int>(items.size()) || !items[index].focusable()) return;
    zone = Zone::Content;
    auto &it = items[index];
    if (sheet == Sheet::Password || sheet == Sheet::Hidden) {
        input_target = it.key == "in:pw" ? 0 : it.key == "in:ssid" ? 1 : -1;
    }
    if (focus != index || it.kind == Kind::Input) {
        set_focus(index);
        if (it.kind == Kind::Input) { sync_list(); return; }
    }
    if (it.kind == Kind::Slider && it.bar) {
        lv_point_t p;
        lv_indev_get_point(lv_indev_active(), &p);
        lv_area_t a;
        lv_obj_get_coords(it.bar, &a);
        if (p.x >= a.x1 - 12 && p.x <= a.x2 + 12) {
            int span = std::max<int>(1, a.x2 - a.x1);
            int v = it.lo + static_cast<int>((std::clamp<int>(p.x - a.x1, 0, span) * static_cast<long>(it.hi - it.lo) + span / 2) / span);
            v = std::clamp(it.lo + (v - it.lo + it.step / 2) / it.step * it.step, it.lo, it.hi);
            if (v != it.cur && it.on_change) it.on_change(v);
            sync_list();
        }
        return;
    }
    if (it.kind == Kind::Choice) { open_picker(it); return; }
    activate();
}

void side_clicked(lv_event_t *e) {
    const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
    if (sheet != Sheet::None) { sheet = Sheet::None; password.clear(); }
    zone = Zone::Sidebar;
    if (index != section) {
        section = index;
        sync_list(true);
        if (section == static_cast<int>(Section::Wifi) && job == Job::None && screen::tick() - last_scan > 20000) start_scan();
    }
    enter_content();
    paint_chrome();
}

void title_clicked(lv_event_t *) { if (sheet != Sheet::None) close_sheet(); }

// ---------------------------------------------------------------- setup

lv_obj_t *make_label(lv_obj_t *parent, const char *text, lv_font_t *f, uint32_t color) {
    auto *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    if (f) lv_obj_set_style_text_font(obj, f, 0);
    return obj;
}

void create_ui() {
    auto *root = lv_screen_active();
    lv_obj_set_style_bg_color(root, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(root, font, 0);
    lv_obj_set_style_text_color(root, lv_color_hex(kText), 0);

    auto *side = lv_obj_create(root);
    lv_obj_remove_flag(side, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(side, 0, 0);
    lv_obj_set_size(side, 196, 340);
    lv_obj_set_style_bg_color(side, lv_color_hex(kSide), 0);
    lv_obj_set_style_border_width(side, 0, 0);
    lv_obj_set_style_radius(side, 0, 0);
    lv_obj_set_style_pad_all(side, 0, 0);
    auto *brand = make_label(side, LV_SYMBOL_SETTINGS "  设置", font, kText);
    lv_obj_set_pos(brand, 18, 10);
    for (int i = 0; i < kSections; ++i) {
        auto *b = lv_obj_create(side);
        side_items[i] = b;
        lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_pos(b, 8, 48 + i * 41);
        lv_obj_set_size(b, 180, 38);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_border_width(b, 0, 0);
        lv_obj_set_style_pad_all(b, 0, 0);
        lv_obj_add_event_cb(b, side_clicked, LV_EVENT_CLICKED, reinterpret_cast<void *>(static_cast<intptr_t>(i)));
        auto *icon = lv_label_create(b);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_18, 0);
        lv_label_set_text(icon, kSectionIcon[i]);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 12, 0);
        auto *text = lv_label_create(b);
        lv_label_set_text(text, kSectionName[i]);
        lv_obj_align(text, LV_ALIGN_LEFT_MID, 44, 0);
    }

    title_label = make_label(root, "", font, kText);
    lv_obj_set_pos(title_label, 214, 9);
    lv_obj_set_width(title_label, 330);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    // Touch back for sub-pages; a large target instead of a small arrow glyph.
    back_button = lv_obj_create(root);
    lv_obj_remove_flag(back_button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(back_button, 206, 3);
    lv_obj_set_size(back_button, 100, 38);
    lv_obj_set_style_radius(back_button, 19, 0);
    lv_obj_set_style_border_width(back_button, 0, 0);
    lv_obj_set_style_pad_all(back_button, 0, 0);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(kRow), 0);
    lv_obj_set_style_bg_color(back_button, lv_color_hex(kFocus), LV_STATE_PRESSED);
    lv_obj_set_ext_click_area(back_button, 6);
    lv_obj_add_event_cb(back_button, title_clicked, LV_EVENT_CLICKED, nullptr);
    auto *back_text = lv_label_create(back_button);
    lv_label_set_text(back_text, LV_SYMBOL_LEFT " 返回");
    lv_obj_set_style_text_color(back_text, lv_color_hex(kText), 0);
    lv_obj_center(back_text);
    lv_obj_add_flag(back_button, LV_OBJ_FLAG_HIDDEN);
    status_label = make_label(root, "", small, kSub);
    lv_obj_set_width(status_label, 240);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(status_label, 548, 13);

    list = lv_obj_create(root);
    lv_obj_set_pos(list, 206, 44);
    lv_obj_set_size(list, 590, 264);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 4, 0);
    lv_obj_set_style_pad_right(list, 10, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    hint_label = make_label(root, "", small, kSub);
    lv_obj_set_pos(hint_label, 214, 314);
    lv_obj_set_width(hint_label, 576);
    lv_label_set_long_mode(hint_label, LV_LABEL_LONG_DOT);

    sync_list(true);
}

void refresh(lv_timer_t *) {
    if (job == Job::None && (sheet == Sheet::None || sheet == Sheet::Network)) {
        wifi = read_wifi();
        if (section == static_cast<int>(Section::Wifi) && sheet == Sheet::None) {
            saved = read_saved();
            if (screen::tick() - last_scan > 30000) start_scan();
        }
        if (sheet != Sheet::Confirm) sync_list();
    }
    paint_chrome();
}
}  // namespace

int main(int argc, char **argv) {
    unsigned long smoke_ms = 0;
    int start_section = 0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--smoke-ms") && i + 1 < argc) {
            char *end = nullptr;
            smoke_ms = std::strtoul(argv[++i], &end, 10);
            if (!end || *end || smoke_ms == 0 || smoke_ms > 3600000) smoke_ms = 0;
        } else if (!std::strcmp(argv[i], "--section") && i + 1 < argc) {
            start_section = std::clamp(std::atoi(argv[++i]), 0, kSections - 1);
        } else {
            std::fprintf(stderr, "Usage: %s [--smoke-ms N] [--section 0-%d]\n", argv[0], kSections - 1);
            return 2;
        }
    }
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);
    if (!screen::open()) { screen::close(); return 1; }
    const char *apps_root = std::getenv("C1_APPS_ROOT");
    const std::string font_path = "A:" + std::string(apps_root && *apps_root ? apps_root : "/storage/apps/current") + "/shared/NotoSansSC-Regular.ttf";
    font = lv_tiny_ttf_create_file(font_path.c_str(), 20);
    small = lv_tiny_ttf_create_file(font_path.c_str(), 16);
    if (!font || !small) std::fprintf(stderr, "Settings font unavailable: %s\n", font_path.c_str());
    // LVGL symbols (Wi-Fi, battery, arrows) come from the built-in font.
    if (font) font->fallback = &lv_font_montserrat_18;
    if (small) small->fallback = &lv_font_montserrat_18;
    if (!font) font = const_cast<lv_font_t *>(&lv_font_montserrat_18);
    if (!small) small = font;
    audio.open();
    wifi = read_wifi();
    saved = read_saved();
    section = start_section;
    create_ui();
    if (section == static_cast<int>(Section::Wifi)) start_scan();
    paint_chrome();
    lv_timer_create(refresh, 3000, nullptr);
    const auto started = screen::tick();
    while (!interrupted && !screen::quit && (!smoke_ms || screen::tick() - started < smoke_ms)) {
        const auto wait_ms = std::min<uint32_t>(lv_timer_handler(), 20);
        for (uint32_t code; (code = screen::take_key()) != 0;) physical_key(code);
        if (interrupted || screen::quit) break;
        poll_jobs();
        poll_sshd_action();
        if (toast_until && screen::tick() >= toast_until) paint_chrome();
        ::usleep(std::max<uint32_t>(wait_ms, 1) * 1000);
    }
    cancel_connect();
    cancel_sshd_action();
    std::fill(password.begin(), password.end(), '\0');
    password.clear();
    audio.close();
    lv_obj_clean(lv_screen_active());
    lv_obj_set_style_text_font(lv_screen_active(), LV_FONT_DEFAULT, 0);
    if (font && font != &lv_font_montserrat_18) lv_tiny_ttf_destroy(font);
    if (small && small != font && small != &lv_font_montserrat_18) lv_tiny_ttf_destroy(small);
    screen::close();
    return 0;
}
