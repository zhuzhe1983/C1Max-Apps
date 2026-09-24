#include "display.hpp"
#include "filter.hpp"
#include "frame.hpp"
#include "photo.hpp"
#include "album.hpp"
#include "feedback.hpp"
#include "shutter_sound.hpp"
#include "lv_tiny_ttf.h"
#include <lvgl.h>
#include "src/misc/cache/instance/lv_image_cache.h"
#include <linux/videodev2.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
struct Mapped { void* data = nullptr; size_t size = 0; };
class V4L2Camera {
    int fd_ = -1;
    Mapped buffers_[3];
    unsigned count_ = 0, w_ = 640, h_ = 480, stride_ = 640;
    bool streaming_ = false;
    uint32_t format_ = V4L2_PIX_FMT_NV12, last_frame_ = 0;
    std::vector<uint8_t> latest_;
    static int ctl(int fd, unsigned long req, void* arg) {
        int r; do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR); return r;
    }
    bool fail(std::string& error, const char* stage) {
        error = std::string(stage) + ": " + strerror(errno);
        fprintf(stderr, "[camera] %s\n", error.c_str()); close(); return false;
    }
public:
    ~V4L2Camera() { close(); }
    void close() {
        std::vector<uint8_t>().swap(latest_);
        if (fd_ < 0) return;
        if (streaming_) { v4l2_buf_type t = V4L2_BUF_TYPE_VIDEO_CAPTURE; ctl(fd_, VIDIOC_STREAMOFF, &t); }
        streaming_ = false;
        for (auto& b : buffers_) { if (b.data) munmap(b.data, b.size); b = {}; }
        count_ = 0;
        v4l2_requestbuffers req{}; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
        ctl(fd_, VIDIOC_REQBUFS, &req); ::close(fd_); fd_ = -1;
    }
    bool open(std::string& error) {
        close(); fd_ = ::open("/dev/video4", O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) return fail(error, "Open /dev/video4");
        v4l2_capability cap{};
        if (ctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) return fail(error, "QUERYCAP");
        uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
        if (!(caps & V4L2_CAP_VIDEO_CAPTURE) || !(caps & V4L2_CAP_STREAMING)) {
            error = "Camera does not support streaming capture"; close(); return false;
        }
        v4l2_format fmt{}; fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        // Half of the ISP's 2048x1944 active mode, preserving its geometry.
        // Convert only display samples in the live view, full RGB at shutter.
        fmt.fmt.pix.width = 1024; fmt.fmt.pix.height = 972;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12; fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (ctl(fd_, VIDIOC_S_FMT, &fmt) < 0) return fail(error, "S_FMT");
        auto& pix = fmt.fmt.pix; w_ = pix.width; h_ = pix.height; format_ = pix.pixelformat;
        if (w_ < 160 || h_ < 120 || w_ > 2048 || h_ > 1944 || (w_ & 1) || (h_ & 1) ||
            (format_ != V4L2_PIX_FMT_NV12 && format_ != V4L2_PIX_FMT_NV21)) {
            error = "Unsupported camera mode"; close(); return false;
        }
        stride_ = camera::nv12_stride(w_, h_, pix.bytesperline, pix.sizeimage,
                                     strncmp((const char*)cap.driver, "ispvideo", sizeof cap.driver) == 0);
        fprintf(stderr, "[camera] %.16s %ux%u bpl=%u stride=%u size=%u\n",
                cap.driver, w_, h_, pix.bytesperline, stride_, pix.sizeimage);
        if (stride_ > 8192 || pix.sizeimage < camera::nv12_size(stride_, h_)) {
            error = "Unsupported NV12 plane layout"; close(); return false;
        }
        v4l2_requestbuffers req{}; req.count = 3; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
        if (ctl(fd_, VIDIOC_REQBUFS, &req) < 0) return fail(error, "REQBUFS");
        if (req.count < 2) { error = "Not enough camera buffers"; close(); return false; }
        count_ = std::min<unsigned>(req.count, 3);
        for (unsigned i = 0; i < count_; ++i) {
            v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP; b.index = i;
            if (ctl(fd_, VIDIOC_QUERYBUF, &b) < 0) return fail(error, "QUERYBUF");
            if (b.length < camera::nv12_size(stride_, h_)) { error = "Camera buffer is too small"; close(); return false; }
            buffers_[i].size = b.length;
            buffers_[i].data = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, b.m.offset);
            if (buffers_[i].data == MAP_FAILED) { buffers_[i].data = nullptr; return fail(error, "mmap"); }
            if (ctl(fd_, VIDIOC_QBUF, &b) < 0) return fail(error, "QBUF");
        }
        v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ctl(fd_, VIDIOC_STREAMON, &type) < 0) return fail(error, "STREAMON");
        streaming_ = true; last_frame_ = screen::tick(); return true;
    }
    int frame(std::string& error) {
        if (fd_ < 0) return -1;
        pollfd p{fd_, POLLIN, 0}; int n = poll(&p, 1, 0);
        if (n < 0 && errno != EINTR) { fail(error, "poll"); return -1; }
        if (n <= 0) {
            if (screen::tick() - last_frame_ > 4000) { error = "Camera frame timeout"; close(); return -1; }
            return 0;
        }
        if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { error = "Camera stream stopped"; close(); return -1; }
        v4l2_buffer b{}; b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP;
        if (ctl(fd_, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN) return 0;
            fail(error, "DQBUF"); return -1;
        }
        if (b.index >= count_) { error = "Invalid camera buffer index"; close(); return -1; }
        if (b.flags & V4L2_BUF_FLAG_ERROR) {
            if (ctl(fd_, VIDIOC_QBUF, &b) < 0) { fail(error, "QBUF"); return -1; }
            if (screen::tick() - last_frame_ > 4000) { error = "Camera repeatedly returned damaged frames"; close(); return -1; }
            return 0;
        }
        const auto& buffer = buffers_[b.index];
        const size_t needed = camera::nv12_size(stride_, h_);
        if (b.bytesused < needed || buffer.size < needed) {
            fprintf(stderr, "[camera] short frame used=%u mapped=%zu stride=%u\n", b.bytesused, buffer.size, stride_);
            error = "Camera produced an incomplete frame"; close(); return -1;
        }
        // Retain an immutable latest frame after QBUF; reading a queued mmap
        // while ISP writes it would tear the preview and saved photo.
        latest_.assign((const uint8_t*)buffer.data, (const uint8_t*)buffer.data + needed);
        if (ctl(fd_, VIDIOC_QBUF, &b) < 0) { fail(error, "QBUF"); return -1; }
        last_frame_ = screen::tick(); return 1;
    }
    // Dimensions of the clockwise-rotated RGB frame, not the raw NV12 sensor.
    unsigned width() const { return h_; }
    unsigned height() const { return w_; }
    bool has_frame() const { return !latest_.empty(); }
    uint32_t pixel(unsigned x, unsigned y) const {
        return camera::nv12_pixel(latest_.data(), stride_, h_, y, h_ - 1 - x, format_ == V4L2_PIX_FMT_NV21);
    }
    bool decode(std::vector<uint32_t>& pixels) const {
        return camera::decode_nv12(latest_.data(), latest_.size(), latest_.size(),
                                    w_, h_, stride_, format_ == V4L2_PIX_FMT_NV21, pixels, true);
    }
};

constexpr uint32_t bg = 0x131918, stage = 0x0b100f, surface = 0x222c29;
constexpr uint32_t ink = 0xf0f2e9, teal = 0x385e50, coral = 0xad4937, muted = 0xaebbb4, accent = 0xb8dbb7;
constexpr unsigned pw = 568, ph = 340;
const char* filter_names[] = {"原色", "黑白", "复古", "暖色", "冷色", "褪色"};
V4L2Camera device;
std::vector<uint32_t> rgb, preview(pw * ph, 0xff263b3c);
lv_font_t* font = nullptr; lv_font_t* small = nullptr;
lv_obj_t *hint = nullptr, *image = nullptr, *status = nullptr, *capture_text = nullptr;
lv_obj_t *camera_error = nullptr, *camera_error_text = nullptr;
lv_obj_t *ratios[4]{}, *filter_label = nullptr, *paper_label = nullptr, *resolution = nullptr, *chooser = nullptr;
lv_obj_t* choices[6]{};
bool chooser_papers = false;
unsigned chooser_index = 0;
lv_image_dsc_t descriptor{};
camera::Filter filter = camera::Filter::Original;
camera::Aspect aspect = camera::Aspect::Landscape;
camera::Paper paper = camera::Paper::None;
camera::FrameArt frame_art[4];
camera::ShutterFeedback feedback;
camera::ShutterSound shutter_sound;
lv_obj_t* flash = nullptr;
std::vector<uint32_t> paper_thumbs[4];
lv_image_dsc_t paper_descriptors[4]{};
bool streaming = false, ready = false;
uint32_t opened_at = 0, saved_at = 0, frames = 0;
uint32_t suppress_enter_until = 0;
enum class View { Camera, Album };
enum class Navigation { None, Camera, Album, AskDelete, CancelDelete, Delete, Filters, Papers, CloseChooser, Capture };
View view = View::Camera;
Navigation navigation = Navigation::None;
std::vector<camera::Photo> photos;
std::vector<uint32_t> album_pixels;
std::string latest_photo;
size_t photo_index = 0;
lv_obj_t *album_image = nullptr, *album_title = nullptr, *album_name = nullptr, *album_error = nullptr;
lv_obj_t *album_previous = nullptr, *album_next = nullptr;
lv_obj_t *album_delete = nullptr, *delete_dialog = nullptr, *delete_message = nullptr;
camera::Photo delete_target;
lv_image_dsc_t album_descriptor{};
volatile sig_atomic_t stopped = 0;
void signal_stop(int) { stopped = 1; }
void shutter(); void save_capture(); void start(); void select_filter(unsigned index);
void select_aspect(unsigned index); void select_paper(unsigned index); void update_preview();
void turn_photo(int direction);
std::string data_root() {
    const char* root = getenv("C1_APPS_DATA"); return root ? root : "/storage/apps/data";
}
std::string assets_root() {
    if (const char* override_path = getenv("C1_CAMERA_ASSETS")) return override_path;
    const char* root = getenv("C1_APPS_ROOT");
    return std::string(root ? root : "/storage/apps/current") + "/camera/assets";
}
const camera::FrameArt* selected_art() { return paper == camera::Paper::None ? nullptr : &frame_art[unsigned(paper)]; }
bool load_frame(unsigned index, std::string& error) {
    const char* names[] = {"", "white", "cream", "film"};
    return index == 0 || frame_art[index].load(assets_root() + "/frames/" + names[index], error);
}
void load_settings() {
    auto path = data_root() + "/camera/settings";
    FILE* f = fopen(path.c_str(), "r"); if (!f) return;
    unsigned a, p, v;
    if (fscanf(f, "v1 %u %u %u", &a, &p, &v) == 3 && a < 4 && p < 4 && v < unsigned(camera::Filter::Count)) {
        aspect = camera::Aspect(a); paper = camera::Paper(p); filter = camera::Filter(v);
    }
    fclose(f);
}
void save_settings() {
    auto dir = data_root() + "/camera"; mkdir(dir.c_str(), 0700);
    auto path = dir + "/settings", temp = dir + "/.settings-XXXXXX";
    std::vector<char> name(temp.begin(), temp.end()); name.push_back(0);
    int fd = mkstemp(name.data()); if (fd < 0) return;
    auto text = std::string("v1 ") + std::to_string(unsigned(aspect)) + " " + std::to_string(unsigned(paper)) + " " + std::to_string(unsigned(filter)) + "\n";
    bool ok = write(fd, text.data(), text.size()) == ssize_t(text.size());
    if (ok) ok = fsync(fd) == 0;
    if (close(fd) != 0) ok = false;
    if (ok) rename(name.data(), path.c_str());
    unlink(name.data());
}

lv_obj_t* label(lv_obj_t* parent, const char* text, int x, int y, int w, uint32_t color = ink, bool compact = false) {
    auto* o = lv_label_create(parent); lv_label_set_text(o, text);
    lv_obj_set_pos(o, x, y); lv_obj_set_width(o, w); lv_label_set_long_mode(o, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    if (compact && small) lv_obj_set_style_text_font(o, small, 0);
    else if (font) lv_obj_set_style_text_font(o, font, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE); return o;
}
lv_obj_t* panel(lv_obj_t* parent, int x, int y, int w, int h, uint32_t color, int radius = 12) {
    auto* o = lv_obj_create(parent); lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0); lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, radius, 0); return o;
}
lv_obj_t* button(lv_obj_t* root, const char* text, int x, int y, int w, int h, lv_event_cb_t callback) {
    auto* o = panel(root, x, y, w, h, teal, 10); lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x547662), LV_STATE_PRESSED);
    auto* caption = label(o, text, 0, (h - 18) / 2, w, ink, true);
    lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(o, callback, LV_EVENT_CLICKED, nullptr); return o;
}
void instructions() { lv_label_set_text(hint, "空格 / 拍照键：快门"); }
void show_camera_error(const std::string& message) {
    lv_label_set_text(camera_error_text, message.c_str());
    if (message.empty()) lv_obj_add_flag(camera_error, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(camera_error, LV_OBJ_FLAG_HIDDEN);
}
void update_controls() {
    for (unsigned i = 0; i < 4; ++i) {
        bool selected = i == unsigned(aspect);
        lv_obj_set_style_bg_color(ratios[i], lv_color_hex(selected ? accent : surface), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(ratios[i], 0), lv_color_hex(selected ? stage : ink), 0);
        lv_obj_set_style_border_width(ratios[i], selected ? 2 : 0, 0);
        lv_obj_set_style_border_color(ratios[i], lv_color_hex(ink), 0);
    }
    lv_label_set_text(filter_label, (std::string("滤镜  ·  ") + filter_names[unsigned(filter)] + "    F").c_str());
    lv_label_set_text(paper_label, (std::string("相纸  ·  ") + camera::paper_name(paper) + "    B").c_str());
    auto c = camera::compose(device.width(), device.height(), aspect, paper);
    std::string dimensions = std::to_string(c.cw) + " × " + std::to_string(c.ch);
    lv_label_set_text(resolution, dimensions.c_str());
}
void close_chooser() {
    if (chooser) lv_obj_delete(chooser);
    chooser = nullptr;
    for (auto& d : paper_descriptors) lv_image_cache_drop(&d);
}
void highlight_choice() {
    for (unsigned i = 0; i < (chooser_papers ? 4u : 6u); ++i) {
        lv_obj_set_style_bg_color(choices[i], lv_color_hex(i == chooser_index ? teal : bg), 0);
        lv_obj_set_style_border_width(choices[i], i == chooser_index ? 2 : 0, 0);
    }
}
void choose(bool papers) {
    close_chooser();
    chooser_papers = papers; chooser_index = papers ? unsigned(paper) : unsigned(filter);
    chooser = panel(lv_screen_active(), 0, 0, 800, 340, stage, 0);
    lv_obj_set_style_bg_opa(chooser, LV_OPA_70, 0);
    lv_obj_add_flag(chooser, LV_OBJ_FLAG_CLICKABLE);
    auto* box = panel(chooser, 122, 48, 556, 244, surface, 16);
    label(box, papers ? "选择相纸" : "选择滤镜", 20, 16, 350);
    label(box, "A / D 选择 · 回车确认 · 返回取消", 20, 46, 516, muted, true);
    button(box, "返回", 440, 8, 96, 44, [](lv_event_t*) { navigation = Navigation::CloseChooser; });
    unsigned count = papers ? 4 : 6;
    for (unsigned i = 0; i < count; ++i) {
        int cols = papers ? 2 : 3, width = papers ? 252 : 165;
        auto* item = panel(box, 20 + (i % cols) * (width + 8), 88 + (i / cols) * 72, width, 60,
                           (papers ? i == unsigned(paper) : i == unsigned(filter)) ? teal : bg, 10);
        choices[i] = item;
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(item, lv_color_hex(teal), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(item, lv_color_hex(accent), 0);
        lv_obj_set_style_border_width(item, (papers ? i == unsigned(paper) : i == unsigned(filter)) ? 2 : 0, 0);
        if (papers) {
            std::string error; load_frame(i, error);
            auto c = camera::compose(device.width(), device.height(), aspect, camera::Paper(i));
            auto& pixels = paper_thumbs[i]; pixels.assign(72*52, 0xff000000u | bg);
            unsigned tw = 72, th = 52;
            if (c.width*th > c.height*tw) th = c.height*tw/c.width; else tw = c.width*th/c.height;
            for (unsigned y = 0; y < th; ++y) for (unsigned x = 0; x < tw; ++x)
                pixels[size_t(y+(52-th)/2)*72+x+(72-tw)/2] = camera::composed_pixel(c, filter, x*c.width/tw,y*c.height/th,
                    [](unsigned sx, unsigned sy) { return device.has_frame() ? device.pixel(sx,sy) : 0xff708572u; }, i ? &frame_art[i] : nullptr);
            auto& d = paper_descriptors[i]; d = {}; d.header.magic = LV_IMAGE_HEADER_MAGIC;
            d.header.cf = LV_COLOR_FORMAT_ARGB8888; d.header.w = 72; d.header.h = 52; d.header.stride = 72*4;
            d.data_size = pixels.size()*4; d.data = (const uint8_t*)pixels.data();
            auto* thumb = lv_image_create(item); lv_image_set_src(thumb, &d); lv_obj_set_pos(thumb, 10, 4);
            lv_obj_remove_flag(thumb, LV_OBJ_FLAG_CLICKABLE);
        }
        label(item, papers ? camera::paper_name(camera::Paper(i)) : filter_names[i], papers ? 96 : 16, 18, width - (papers ? 104 : 24), ink, true);
        lv_obj_add_event_cb(item, [](lv_event_t* e) {
            auto value = uintptr_t(lv_event_get_user_data(e));
            if (value & 0x100) select_paper(value & 0xff); else select_filter(value);
            navigation = Navigation::CloseChooser;
        }, LV_EVENT_CLICKED, (void*)uintptr_t(i | (papers ? 0x100 : 0)));
    }
}
void ui() {
    auto* root = lv_screen_active(); lv_obj_clean(root); chooser = nullptr;
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root, lv_color_hex(bg), 0);
    if (font) lv_obj_set_style_text_font(root, font, 0);
    descriptor = {}; descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    descriptor.header.cf = LV_COLOR_FORMAT_ARGB8888; descriptor.header.w = pw; descriptor.header.h = ph;
    descriptor.header.stride = pw * 4; descriptor.data_size = preview.size() * 4;
    descriptor.data = (const uint8_t*)preview.data(); image = lv_image_create(root);
    lv_image_set_src(image, &descriptor); lv_obj_set_pos(image, 0, 0);
    panel(root, 568, 0, 1, 340, 0x36413c, 0);
    label(root, "拍立得", 584, 10, 100);
    label(root, "INSTANT", 701, 13, 90, accent, true);
    // Keep every capture status in the control rail, outside all preview ratios.
    status = label(root, "正在启动…", 584, 42, 204, accent, true);
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    resolution = label(root, "", 584, 74, 136, muted, true);
    label(root, "切换 R", 726, 74, 70, muted, true);
    for (unsigned i = 0; i < 4; ++i) {
        auto* item = panel(root, 584 + i * 52, 98, 48, 44, surface, 8);
        ratios[i] = item; lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(item, lv_color_hex(teal), LV_STATE_PRESSED);
        lv_obj_add_event_cb(item, [](lv_event_t* e) { select_aspect(uintptr_t(lv_event_get_user_data(e))); },
                           LV_EVENT_CLICKED, (void*)uintptr_t(i));
        auto* text = label(item, camera::aspect_name(camera::Aspect(i)), 0, 12, 48, ink, true);
        lv_obj_set_style_text_align(text, LV_TEXT_ALIGN_CENTER, 0);
    }
    auto* f = button(root, "", 584, 154, 204, 44, [](lv_event_t*) { navigation = Navigation::Filters; });
    filter_label = lv_obj_get_child(f, 0); lv_obj_set_style_bg_color(f, lv_color_hex(surface), 0);
    auto* b = button(root, "", 584, 206, 204, 44, [](lv_event_t*) { navigation = Navigation::Papers; });
    paper_label = lv_obj_get_child(b, 0); lv_obj_set_style_bg_color(b, lv_color_hex(surface), 0);
    auto* capture = button(root, "拍摄", 584, 262, 124, 48, [](lv_event_t*) { navigation = Navigation::Capture; });
    lv_obj_set_style_bg_color(capture, lv_color_hex(coral), 0); capture_text = lv_obj_get_child(capture, 0);
    button(root, "相册 G", 716, 262, 72, 48, [](lv_event_t*) { navigation = Navigation::Album; });
    hint = label(root, "", 584, 319, 204, muted, true);
    camera_error = panel(root, 24, 222, 520, 98, surface, 12);
    camera_error_text = label(camera_error, "", 16, 14, 488, ink, true);
    show_camera_error("");
    flash = panel(root, 0, 0, 800, 340, 0xffffff, 0);
    lv_obj_add_flag(flash, LV_OBJ_FLAG_HIDDEN);
    update_controls(); instructions();
}
void update_preview() {
    if (!ready || !device.has_frame()) return;
    const auto c = camera::compose(device.width(), device.height(), aspect, paper);
    unsigned dw = pw, dh = ph;
    if (c.width * ph > c.height * pw) dh = c.height * pw / c.width;
    else dw = c.width * ph / c.height;
    unsigned x0 = (pw - dw) / 2, y0 = (ph - dh) / 2;
    std::fill(preview.begin(), preview.end(), 0xff000000u | stage);
    for (unsigned y = 0; y < dh; ++y) for (unsigned x = 0; x < dw; ++x) {
        preview[size_t(y + y0) * pw + x + x0] = camera::composed_pixel(c, filter, x * c.width / dw, y * c.height / dh,
                                                  [](unsigned sx, unsigned sy) { return device.pixel(sx, sy); }, selected_art());
    }
    lv_obj_invalidate(image);
}
void select_filter(unsigned index) {
    if (feedback.active) return;
    filter = camera::Filter(index % unsigned(camera::Filter::Count));
    update_controls(); update_preview(); save_settings();
}
void select_aspect(unsigned index) {
    if (feedback.active) return;
    aspect = camera::Aspect(index % 4);
    update_controls(); update_preview(); save_settings();
}
void select_paper(unsigned index) {
    if (feedback.active) return;
    paper = camera::Paper(index % 4);
    std::string error; if (!load_frame(unsigned(paper), error)) show_camera_error(error); else show_camera_error("");
    update_controls(); update_preview(); save_settings();
}
void start() {
    ready = false; saved_at = 0; rgb.clear(); std::string error;
    show_camera_error("");
    std::fill(preview.begin(), preview.end(), 0xff000000u | stage); lv_obj_invalidate(image);
    lv_label_set_text(status, "正在启动摄像头…"); lv_label_set_text(hint, "正在等待画面…");
    lv_label_set_text(capture_text, "拍摄"); lv_timer_handler();
    streaming = device.open(error); update_controls(); opened_at = screen::tick(); frames = 0;
    std::string art_error; if (!load_frame(unsigned(paper), art_error)) show_camera_error(art_error);
    if (!streaming) {
        lv_label_set_text(status, "未就绪 · 按快门重试"); lv_label_set_text(capture_text, "重试");
        show_camera_error("摄像头启动失败，可按快门重试。\n" + error);
    }
}
void shutter() {
    if (feedback.active) return;
    if (!streaming) { start(); return; }
    if (!ready || !device.has_frame()) { lv_label_set_text(status, "启动中，请稍候…"); return; }
    std::string error;
    if (!load_frame(unsigned(paper), error)) { show_camera_error(error); return; }
    if (!feedback.begin(screen::tick())) return;
    // Freeze the photo before visual feedback; the white flash is UI only.
    if (!device.decode(rgb)) {
        feedback.complete(screen::tick()); lv_label_set_text(status, "画面不完整，请重试"); return;
    }
    feedback.started = screen::tick(); saved_at = 0;
    show_camera_error(""); lv_label_set_text(status, "拍摄中…");
    lv_obj_set_style_bg_opa(flash, feedback.opacity(screen::tick()), 0);
    lv_obj_remove_flag(flash, LV_OBJ_FLAG_HIDDEN); lv_refr_now(nullptr);
    screen::video_refresh();
    shutter_sound.play(assets_root() + "/shutter.wav", screen::tick());
    fprintf(stderr, "[camera] shutter flash begin\n");
}
void save_capture() {
    lv_obj_add_flag(flash, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(status, "正在保存…"); lv_refr_now(nullptr); screen::video_refresh();
    std::string path, error;
    uint32_t began = screen::tick();
    if (camera::save_photo(data_root(), rgb, device.width(), device.height(), filter, path, error, aspect, paper, selected_art())) {
        latest_photo = path;
        lv_label_set_text(status, "已保存 · G 查看"); instructions();
        fprintf(stderr, "[camera] saved %s (%s, paper=%u, %u ms)\n", path.c_str(), camera::aspect_name(aspect), unsigned(paper), screen::tick() - began);
    } else {
        lv_label_set_text(status, "保存失败"); show_camera_error("照片未保存，可按快门重试。\n" + error);
        fprintf(stderr, "[camera] %s\n", error.c_str());
    }
    std::vector<uint32_t>().swap(rgb);
    saved_at = screen::tick();
    feedback.complete(saved_at);
}
void show_photo() {
    lv_obj_add_flag(album_image, LV_OBJ_FLAG_HIDDEN);
    lv_image_set_src(album_image, nullptr); // Release the previous descriptor before replacing its pixels.
    lv_image_cache_drop(&album_descriptor);
    album_pixels.clear();
    lv_obj_remove_flag(album_error, LV_OBJ_FLAG_HIDDEN);
    std::string title = "相册  /  " + std::to_string(photos.empty() ? 0 : photo_index + 1) + " / " + std::to_string(photos.size());
    lv_label_set_text(album_title, title.c_str());
    if (photos.empty()) {
        lv_label_set_text(album_name, "拍下的照片会保存在这里");
        lv_label_set_text(album_error, "还没有照片\n点击「返回拍摄」记录第一张");
    } else {
        const auto& photo = photos[photo_index];
        lv_label_set_text(album_name, photo.name.c_str());
        lv_label_set_text(album_error, "正在读取照片…");
        unsigned w = 0, h = 0; std::string error;
        if (camera::load_photo(photo.path, 568, 260, album_pixels, w, h, error)) {
            album_descriptor = {}; album_descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
            album_descriptor.header.cf = LV_COLOR_FORMAT_ARGB8888;
            album_descriptor.header.w = w; album_descriptor.header.h = h; album_descriptor.header.stride = w * 4;
            album_descriptor.data_size = album_pixels.size() * 4; album_descriptor.data = (const uint8_t*)album_pixels.data();
            lv_image_set_src(album_image, &album_descriptor); lv_obj_set_pos(album_image, (800 - int(w)) / 2, 52 + (260 - int(h)) / 2);
            lv_obj_remove_flag(album_image, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(album_error, LV_OBJ_FLAG_HIDDEN);
            fprintf(stderr, "[camera] album %zu/%zu %ux%u\n", photo_index + 1, photos.size(), w, h);
        } else {
            lv_label_set_text(album_error, (error + "\n可以继续翻页查看其他照片").c_str());
            fprintf(stderr, "[camera] album decode: %s\n", error.c_str());
        }
    }
    // At the ends, keep the current image rather than wrapping unexpectedly.
    bool previous = !photos.empty() && photo_index > 0;
    bool next = !photos.empty() && photo_index + 1 < photos.size();
    for (auto entry : {std::make_pair(album_previous, previous), std::make_pair(album_next, next)}) {
        lv_obj_set_style_bg_color(entry.first, lv_color_hex(entry.second ? teal : surface), 0);
        if (entry.second) lv_obj_add_flag(entry.first, LV_OBJ_FLAG_CLICKABLE);
        else lv_obj_remove_flag(entry.first, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_set_style_bg_color(album_delete, lv_color_hex(photos.empty() ? surface : coral), 0);
    if (photos.empty()) lv_obj_remove_flag(album_delete, LV_OBJ_FLAG_CLICKABLE);
    else lv_obj_add_flag(album_delete, LV_OBJ_FLAG_CLICKABLE);
}
void cancel_delete() {
    if (delete_dialog) lv_obj_delete(delete_dialog);
    delete_dialog = delete_message = nullptr; delete_target = {};
}
void ask_delete() {
    if (view != View::Album || photos.empty() || delete_dialog) return;
    delete_target = photos[photo_index];
    delete_dialog = panel(lv_screen_active(), 0, 0, 800, 340, 0x19241f, 0);
    lv_obj_set_style_bg_opa(delete_dialog, LV_OPA_60, 0);
    lv_obj_add_flag(delete_dialog, LV_OBJ_FLAG_CLICKABLE);
    auto* box = panel(delete_dialog, 150, 63, 500, 215, surface, 16);
    label(box, "删除这张照片？", 24, 20, 452);
    auto* name = label(box, delete_target.name.c_str(), 24, 55, 452, muted, true);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    delete_message = label(box, "删除后无法恢复。", 24, 86, 452, ink, true);
    button(box, "取消（返回）", 24, 149, 214, 42, [](lv_event_t*) { navigation = Navigation::CancelDelete; });
    auto* confirm = button(box, "删除（回车）", 262, 149, 214, 42, [](lv_event_t*) { navigation = Navigation::Delete; });
    lv_obj_set_style_bg_color(confirm, lv_color_hex(coral), 0);
}
void confirm_delete() {
    if (!delete_dialog) return;
    std::string error;
    if (!camera::delete_photo(data_root(), delete_target, error)) {
        lv_label_set_text(delete_message, error.c_str());
        fprintf(stderr, "[camera] delete failed: %s\n", error.c_str()); return;
    }
    fprintf(stderr, "[camera] deleted %s\n", delete_target.path.c_str());
    if (latest_photo == delete_target.path) latest_photo.clear();
    auto found = std::find_if(photos.begin(), photos.end(), [](const camera::Photo& p) { return p.path == delete_target.path; });
    if (found != photos.end()) photos.erase(found);
    photo_index = photos.empty() ? 0 : std::min(photo_index, photos.size() - 1);
    // Ignore a held confirmation key's auto-repeat after dismissing the dialog.
    suppress_enter_until = screen::tick() + 800;
    cancel_delete(); show_photo();
}
void turn_photo(int direction) {
    if (photos.empty() || (direction < 0 && photo_index == 0) ||
        (direction > 0 && photo_index + 1 >= photos.size())) return;
    if (direction < 0) --photo_index; else ++photo_index;
    show_photo();
}
void open_album() {
    view = View::Album; device.close(); streaming = ready = false; saved_at = 0;
    std::vector<uint32_t>().swap(rgb);
    std::string error; camera::list_photos(data_root(), photos, error); photo_index = 0;
    // Open the photo just taken even if several captures share a timestamp.
    for (size_t i = 0; i < photos.size(); ++i) if (photos[i].path == latest_photo) { photo_index = i; break; }
    auto* root = lv_screen_active(); lv_obj_clean(root);
    image = hint = status = capture_text = nullptr;
    panel(root, 108, 0, 584, 340, stage, 0);
    album_title = label(root, "相册", 18, 9, 400);
    album_delete = button(root, "删除  X", 520, 4, 112, 44, [](lv_event_t*) { navigation = Navigation::AskDelete; });
    button(root, "返回拍摄", 640, 4, 148, 44, [](lv_event_t*) { navigation = Navigation::Camera; });
    album_previous = button(root, "‹ 上一张", 14, 150, 94, 44, [](lv_event_t*) { turn_photo(-1); });
    album_next = button(root, "下一张 ›", 692, 150, 94, 44, [](lv_event_t*) { turn_photo(1); });
    album_image = lv_image_create(root);
    album_error = label(root, "", 155, 144, 490, muted);
    lv_obj_set_style_text_align(album_error, LV_TEXT_ALIGN_CENTER, 0);
    album_name = label(root, "", 18, 318, 463, muted, true);
    lv_label_set_long_mode(album_name, LV_LABEL_LONG_DOT);
    label(root, "A / D：翻页    返回键：取景", 501, 318, 285, muted, true);
    show_photo();
    if (!error.empty()) lv_label_set_text(album_error, error.c_str());
}
void open_camera() {
    view = View::Camera; ui();
    album_image = album_title = album_name = album_error = album_previous = album_next = nullptr;
    lv_image_cache_drop(&album_descriptor);
    std::vector<uint32_t>().swap(album_pixels); photos.clear(); start();
}
void key(uint32_t k) {
    if (k == screen::KEY_HOME) { screen::quit = true; return; }
    if (feedback.active) return;
    if (k == LV_KEY_ENTER && suppress_enter_until && int32_t(suppress_enter_until - screen::tick()) > 0) {
        suppress_enter_until = screen::tick() + 800; return;
    }
    if (view == View::Album) {
        if (delete_dialog) {
            if (k == LV_KEY_ENTER) navigation = Navigation::Delete;
            else if (k == screen::KEY_EXIT || k == 'n' || k == 'N') navigation = Navigation::CancelDelete;
            return;
        }
        if (k == 'x' || k == 'X' || k == LV_KEY_BACKSPACE) { navigation = Navigation::AskDelete; return; }
        if (k == 'a' || k == 'A' || k == LV_KEY_LEFT) turn_photo(-1);
        else if (k == 'd' || k == 'D' || k == LV_KEY_RIGHT) turn_photo(1);
        else if (k == screen::KEY_EXIT || k == screen::KEY_SYMBOL || k == LV_KEY_ENTER || k == ' ' || k == 'g' || k == 'G') navigation = Navigation::Camera;
        return;
    }
    if (chooser) {
        unsigned count = chooser_papers ? 4 : 6;
        if (k == screen::KEY_EXIT) navigation = Navigation::CloseChooser;
        else if (k == LV_KEY_ENTER) {
            if (chooser_papers) select_paper(chooser_index); else select_filter(chooser_index);
            navigation = Navigation::CloseChooser;
        } else if (k == 'a' || k == 'A' || k == LV_KEY_LEFT) {
            chooser_index = (chooser_index + count - 1) % count; highlight_choice();
        } else if (k == 'd' || k == 'D' || k == LV_KEY_RIGHT) {
            chooser_index = (chooser_index + 1) % count; highlight_choice();
        }
        return;
    }
    if (k == 'g' || k == 'G') { navigation = Navigation::Album; return; }
    if (k == 'r' || k == 'R') { select_aspect(unsigned(aspect) + 1); return; }
    if (k == 'b' || k == 'B') { select_paper(unsigned(paper) + 1); return; }
    if (k == 'f' || k == 'F' || k == LV_KEY_RIGHT) select_filter(unsigned(filter) + 1);
    else if (k == LV_KEY_LEFT) select_filter(unsigned(filter) + unsigned(camera::Filter::Count) - 1);
    else if (k == ' ' || k == LV_KEY_ENTER || k == screen::KEY_SYMBOL) shutter();
}
}
int main() {
    signal(SIGINT, signal_stop); signal(SIGTERM, signal_stop);
    if (!screen::open()) return 1;
    // Reuse the complete-frame presenter for camera UI/flash animation. Never
    // paint LVGL strips into the framebuffer currently being scanned out.
    if (!screen::video_begin()) { screen::close(); return 1; }
    screen::video_controls(true, true);
    const char* root = getenv("C1_APPS_ROOT");
    std::string font_path = "A:" + std::string(root ? root : "/storage/apps/current") + "/shared/NotoSansSC-Regular.ttf";
    font = lv_tiny_ttf_create_file(font_path.c_str(), 18); small = lv_tiny_ttf_create_file(font_path.c_str(), 16);
    load_settings(); ui(); start();
    while (!stopped && !screen::quit) {
        if (streaming && !feedback.active) {
            std::string error; int got = device.frame(error);
            if (got < 0) {
                streaming = ready = false; saved_at = 0; rgb.clear();
                lv_label_set_text(status, "已停止 · 按快门重试"); lv_label_set_text(capture_text, "重试");
                show_camera_error("摄像头已停止，可按快门重试。\n" + error); fprintf(stderr, "[camera] %s\n", error.c_str());
            } else if (got > 0) {
                ++frames;
                // Do not save the black frames produced while auto-exposure starts.
                if (!ready && screen::tick() - opened_at >= 800 && frames >= 5) {
                    ready = true; lv_label_set_text(status, "实时取景"); instructions();
                    fprintf(stderr, "[camera] preview ready after %u frames\n", frames);
                }
                update_preview();
            }
        }
        shutter_sound.poll(screen::tick());
        if (feedback.active) {
            if (feedback.save_due(screen::tick())) save_capture();
            else lv_obj_set_style_bg_opa(flash, feedback.opacity(screen::tick()), 0);
        }
        if (saved_at && screen::tick() - saved_at > 3500) {
            saved_at = 0; lv_label_set_text(status, "实时取景"); instructions();
        }
        lv_timer_handler(); screen::video_refresh();
        for (uint32_t k; (k = screen::take_key());) key(k);
        // Rebuild screens outside LVGL callbacks; never restart its timer handler recursively.
        auto next = navigation; navigation = Navigation::None;
        if (feedback.active) next = Navigation::None;
        if (!screen::quit && next == Navigation::Album) open_album();
        else if (!screen::quit && next == Navigation::Camera) open_camera();
        else if (!screen::quit && next == Navigation::AskDelete) ask_delete();
        else if (!screen::quit && next == Navigation::CancelDelete) cancel_delete();
        else if (!screen::quit && next == Navigation::Delete) confirm_delete();
        else if (!screen::quit && next == Navigation::Filters) choose(false);
        else if (!screen::quit && next == Navigation::Papers) choose(true);
        else if (!screen::quit && next == Navigation::CloseChooser) close_chooser();
        else if (!screen::quit && next == Navigation::Capture) shutter();
        usleep(12000);
    }
    fprintf(stderr, "[camera] closed after %u frames\n", frames);
    shutter_sound.stop();
    device.close(); lv_obj_clean(lv_screen_active()); lv_obj_set_style_text_font(lv_screen_active(), LV_FONT_DEFAULT, 0);
    if (font) lv_tiny_ttf_destroy(font);
    if (small) lv_tiny_ttf_destroy(small);
    screen::close(); return 0;
}
