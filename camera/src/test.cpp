#include "frame.hpp"
#include "photo.hpp"
#include "album.hpp"
#include "feedback.hpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char** argv) {
    assert(argc == 3);
    using namespace camera;
    ShutterFeedback feedback;
    assert(feedback.begin(1000) && feedback.opacity(1000) == 240);
    assert(!feedback.begin(1050) && !feedback.save_due(1179));
    assert(feedback.opacity(1080) > 0 && feedback.opacity(1080) < 240);
    assert(!feedback.save_due(1180) && feedback.opacity(1180) == 0);
    assert(feedback.save_due(1240));
    feedback.complete(1500);
    assert(!feedback.begin(1899) && feedback.begin(1900));
    feedback.complete(UINT32_MAX - 100);
    assert(!feedback.begin(20) && feedback.begin(400));
    assert(alpha_over(0xff102030, 0x00abcdef) == 0xff102030);
    assert(alpha_over(0xff102030, 0xffabcdef) == 0xffabcdef);
    assert(alpha_over(0xff000000, 0x80ffffff) == 0xff808080);
    assert(nv12_stride(640, 480, 960, 460800, true) == 640);
    assert(nv12_stride(640, 480, 960, 460800, false) == 960);
    assert(nv12_stride(640, 480, 672, 483840, true) == 672);
    assert(nv12_stride(640, 480, 960, 691200, true) == 960);
    std::vector<uint32_t> out;
    uint8_t padded[] = {16,235,9,9,16,235,9,9,128,128,9,9};
    assert(decode_nv12(padded, sizeof padded, sizeof padded, 2, 2, 4, false, out));
    assert(out == std::vector<uint32_t>({0xff000000,0xffffffff,0xff000000,0xffffffff}));
    uint8_t red[] = {81,81,81,81,90,240};
    assert(decode_nv12(red, 6, 6, 2, 2, 2, false, out));
    assert(((out[0] >> 16) & 255) >= 250 && (out[0] & 255) < 4);
    auto reference = out;
    std::swap(red[4], red[5]);
    assert(decode_nv12(red, 6, 6, 2, 2, 2, true, out) && out == reference);
    for (size_t n = 0; n < sizeof padded; ++n) {
        assert(!decode_nv12(padded, n, sizeof padded, 2, 2, 4, false, out));
        assert(!decode_nv12(padded, sizeof padded, n, 2, 2, 4, false, out));
    }
    assert(!decode_nv12(padded, 12, 12, 3, 2, 4, false, out));
    assert(!decode_nv12(padded, 12, 12, 2, 3, 4, false, out));
    assert(!decode_nv12(padded, 12, 12, 2, 2, 1, false, out));
    uint8_t asymmetric[] = {16,45,80,100,130,150,190,235,128,128,128,128};
    std::vector<uint32_t> clockwise;
    assert(decode_nv12(asymmetric, 12, 12, 4, 2, 4, false, out));
    assert(decode_nv12(asymmetric, 12, 12, 4, 2, 4, false, clockwise, true));
    assert(clockwise == std::vector<uint32_t>({out[4],out[0],out[5],out[1],out[6],out[2],out[7],out[3]}));
    std::string root = argv[1], path, previous, error;
    std::vector<uint32_t> pixels(32 * 24, 0xff804020);
    for (unsigned i = 0; i < unsigned(Filter::Count); ++i) {
        assert(save_photo(root, pixels, 32, 24, Filter(i), path, error));
        assert(path != previous && path.find(root + "/camera/photos/print-") == 0);
        struct stat st{}; assert(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
        std::ifstream in(path, std::ios::binary);
        std::vector<unsigned char> bytes{std::istreambuf_iterator<char>(in), {}};
        assert(bytes.size() > 100 && bytes[0] == 0xff && bytes[1] == 0xd8);
        assert(bytes[bytes.size()-2] == 0xff && bytes.back() == 0xd9);
        previous = path;
    }
    assert(!save_photo(root, pixels, 32, 25, Filter::Original, path, error) && path.empty());
    assert(!save_photo(root + "/missing/parent", pixels, 32, 24, Filter::Original, path, error));
    // A file in place of camera/ must be reported, never treated as a directory.
    std::string blocked = root + "/blocked"; assert(mkdir(blocked.c_str(), 0700) == 0);
    std::ofstream(blocked + "/camera").put('x');
    assert(!save_photo(blocked, pixels, 32, 24, Filter::Original, path, error));
    // Force buffered fwrite/fflush to fail after the file was created.
    // The failed capture must leave neither a JPEG nor a partial temp file.
    struct rlimit old_limit{}, short_limit{};
    assert(getrlimit(RLIMIT_FSIZE, &old_limit) == 0);
    short_limit = old_limit; short_limit.rlim_cur = 1;
    auto old_signal = signal(SIGXFSZ, SIG_IGN);
    assert(setrlimit(RLIMIT_FSIZE, &short_limit) == 0);
    bool written = save_photo(root, pixels, 32, 24, Filter::Original, path, error);
    assert(setrlimit(RLIMIT_FSIZE, &old_limit) == 0);
    signal(SIGXFSZ, old_signal);
    assert(!written && path.empty());
    std::vector<Photo> photos;
    assert(list_photos(root + "/absent", photos, error) && photos.empty());
    assert(!list_photos(blocked, photos, error) && !error.empty());
    assert(list_photos(root, photos, error) && photos.size() == 6);
    for (const auto& photo : photos) {
        unsigned w = 0, h = 0;
        assert(load_photo(photo.path, 16, 20, out, w, h, error));
        assert(w == 16 && h == 12 && out.size() == w * h);
        assert(load_photo(photo.path, 40, 6, out, w, h, error));
        assert(w == 8 && h == 6);
    }
    auto oldest = photos.back().path;
    timespec times[2] = {{1, 0}, {1, 0}};
    assert(utimensat(AT_FDCWD, oldest.c_str(), times, 0) == 0);
    assert(list_photos(root, photos, error) && photos.back().path == oldest);
    auto folder = root + "/camera/photos";
    std::ofstream(folder + "/unfinished").put('x');
    assert(mkdir((folder + "/directory.jpg").c_str(), 0700) == 0);
    assert(symlink(previous.c_str(), (folder + "/link.jpg").c_str()) == 0);
    assert(list_photos(root, photos, error) && photos.size() == 6);
    unsigned w = 1, h = 1;
    assert(!load_photo(folder + "/link.jpg", 20, 20, out, w, h, error) && out.empty() && !w && !h);
    std::ifstream photo_file(previous, std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(photo_file), {}};
    std::string broken = blocked + "/bad.jpg";
    for (size_t n : {size_t(0), size_t(5), bytes.size()/2, bytes.size()-1}) {
        { std::ofstream file(broken, std::ios::binary); file.write(bytes.data(), n); }
        assert(!load_photo(broken, 20, 20, out, w, h, error) && out.empty() && !w && !h);
    }
    assert(unlink((folder + "/unfinished").c_str()) == 0);
    assert(unlink((folder + "/link.jpg").c_str()) == 0);
    assert(rmdir((folder + "/directory.jpg").c_str()) == 0);
    puts("Album scan, sort, aspect-fit, JPEG decode and broken-file tests passed");
    std::string deletion_root = root + "/delete-test";
    assert(mkdir(deletion_root.c_str(), 0700) == 0);
    for (int i = 0; i < 3; ++i) assert(save_photo(deletion_root, pixels, 32, 24, Filter::Original, path, error));
    assert(list_photos(deletion_root, photos, error) && photos.size() == 3);
    auto middle = photos[1];
    assert(delete_photo(deletion_root, middle, error) && access(middle.path.c_str(), F_OK) != 0);
    assert(!delete_photo(deletion_root, middle, error) && !error.empty());
    auto selected = photos[0], invalid = selected;
    invalid.name = "../escape.jpg"; invalid.path = deletion_root + "/camera/photos/" + invalid.name;
    assert(!delete_photo(deletion_root, invalid, error));
    assert(access(selected.path.c_str(), F_OK) == 0);
    invalid = selected; invalid.path = previous;
    assert(!delete_photo(deletion_root, invalid, error) && access(previous.c_str(), F_OK) == 0);
    std::string moved = selected.path + ".backup";
    assert(rename(selected.path.c_str(), moved.c_str()) == 0);
    assert(symlink(moved.c_str(), selected.path.c_str()) == 0);
    assert(!delete_photo(deletion_root, selected, error));
    assert(unlink(selected.path.c_str()) == 0);
    std::ofstream(selected.path).put('x'); // Same name, different file: never delete the replacement.
    assert(!delete_photo(deletion_root, selected, error) && access(selected.path.c_str(), F_OK) == 0);
    assert(unlink(selected.path.c_str()) == 0);
    assert(rename(moved.c_str(), selected.path.c_str()) == 0);
    assert(delete_photo(deletion_root, selected, error));
    assert(list_photos(deletion_root, photos, error) && photos.size() == 1);
    assert(delete_photo(deletion_root, photos.front(), error));
    assert(list_photos(deletion_root, photos, error) && photos.empty());
    puts("Album deletion, missing/replaced files and path protection tests passed");
    std::string rotation_root = root + "/rotation-test";
    assert(mkdir(rotation_root.c_str(), 0700) == 0);
    std::vector<uint8_t> nv12(640 * 480 * 3 / 2, 128);
    const uint8_t luminance[] = {48,96,160,224};
    for (unsigned y = 0; y < 480; ++y) for (unsigned x = 0; x < 640; ++x)
        nv12[y * 640 + x] = luminance[(y >= 240 ? 2 : 0) + (x >= 320 ? 1 : 0)];
    assert(decode_nv12(nv12.data(), nv12.size(), nv12.size(), 640, 480, 640, false, out));
    uint32_t expected[] = {out[360*640+160],out[120*640+160],out[360*640+480],out[120*640+480]};
    auto check_upright = [&](const std::vector<uint32_t>& im, unsigned iw, unsigned ih) {
        assert(iw == 202 && ih == 270);
        for (unsigned q = 0; q < 4; ++q) {
            uint32_t actual = im[size_t(ih*(q/2*2+1)/4)*iw + iw*(q%2*2+1)/4];
            for (int shift : {0,8,16}) assert(std::abs(int((actual>>shift)&255)-int((expected[q]>>shift)&255)) <= 3);
        }
    };
    // A legacy camera JPEG gets a read-only correction; an imported landscape
    // JPEG keeps its stored orientation. New portrait JPEGs must not rotate twice.
    assert(save_photo(rotation_root, out, 640, 480, Filter::Original, path, error));
    std::string legacy = rotation_root + "/camera/photos/instant-20260924-123456-ABC123.jpg";
    assert(rename(path.c_str(), legacy.c_str()) == 0);
    assert(load_photo(legacy, 544, 270, pixels, w, h, error)); check_upright(pixels, w, h);
    std::string imported = rotation_root + "/camera/photos/imported.jpg";
    { std::ifstream in(legacy, std::ios::binary); std::ofstream file(imported, std::ios::binary); file << in.rdbuf(); }
    assert(load_photo(imported, 544, 270, pixels, w, h, error) && w == 360 && h == 270);
    assert(std::abs(int(pixels[67*w+90]&255)-int(out[120*640+160]&255)) <= 3);
    assert(decode_nv12(nv12.data(), nv12.size(), nv12.size(), 640, 480, 640, false, clockwise, true));
    assert(save_photo(rotation_root, clockwise, 480, 640, Filter::Original, path, error));
    assert(load_photo(path, 544, 270, pixels, w, h, error)); check_upright(pixels, w, h);
    puts("Clockwise rotation, portrait JPEG and legacy album orientation tests passed");
    // Crop is centered, keeps exact requested geometry, and never stretches.
    auto landscape = compose(972, 1024, Aspect::Landscape, Paper::None);
    assert(landscape.cw == 972 && landscape.ch == 729 && landscape.x == 0 && landscape.y == 147);
    auto portrait = compose(972, 1024, Aspect::Portrait, Paper::None);
    assert(portrait.cw == 768 && portrait.ch == 1024 && portrait.x == 102 && portrait.y == 0);
    auto square = compose(972, 1024, Aspect::Square, Paper::None);
    assert(square.cw == 972 && square.ch == 972 && square.y == 26);
    auto wide = compose(972, 1024, Aspect::Wide, Paper::None);
    assert(wide.cw == 960 && wide.ch == 540 && wide.x == 6 && wide.y == 242);
    assert(!compose(0, 640, Aspect::Portrait, Paper::None).width);
    assert(!compose(4, 4, Aspect::Wide, Paper::None).width);
    std::string formats_root = root + "/formats-test";
    assert(mkdir(formats_root.c_str(), 0700) == 0);
    std::vector<uint32_t> chart(972 * 1024);
    for (unsigned y = 0; y < 1024; ++y) for (unsigned x = 0; x < 972; ++x)
        chart[size_t(y)*972+x] = 0xff000000u | (x * 255 / 971 << 16) | (y * 255 / 1023 << 8) | 0x40;
    for (unsigned a = 0; a < 4; ++a) for (unsigned p = 0; p < 4; ++p) {
        FrameArt art; std::string asset_error;
        const char* names[] = {"", "white", "cream", "film"};
        if (p) assert(art.load(std::string(argv[2])+"/"+names[p], asset_error) && art.ready());
        auto c = compose(972, 1024, Aspect(a), Paper(p));
        if (p) {
            assert(art.sample(c.left+c.cw/2,c.top+c.ch/2,c.width,c.height,c.left,c.top,c.cw,c.ch) == 0);
            assert(art.sample(c.width,0,c.width,c.height,c.left,c.top,c.cw,c.ch) == 0);
            for (unsigned y : {0u,c.top/2,c.height-c.top}) {
                auto expected = alpha_over(0xff0b100f,art.sample(c.width/2,y,c.width,c.height,c.left,c.top,c.cw,c.ch));
                auto actual = composed_pixel(c,Filter::Original,c.width/2,y,[](unsigned,unsigned){ return 0xff000000u; }, &art);
                assert(expected == actual);
            }
        }
        assert(c.x + c.cw <= 972 && c.y + c.ch <= 1024);
        assert(c.width == c.cw + c.left*2);
        assert(c.height == c.ch + c.top*(p == 3 ? 2 : 4));
        auto sample = [&](unsigned x, unsigned y) { assert(x < 972 && y < 1024); return chart[size_t(y)*972+x]; };
        auto first = composed_pixel(c, Filter::Original, c.left, c.top, sample);
        assert(first == chart[size_t(c.y)*972+c.x]);
        // Full image and small viewfinder use the same source sample/filter.
        for (unsigned sy = 0; sy < 127; ++sy) for (unsigned sx = 0; sx < 173; ++sx) {
            unsigned x = sx*c.width/173, y = sy*c.height/127;
            uint32_t pixel = composed_pixel(c, Filter::Faded, x, y, sample);
            if (x >= c.left && y >= c.top && x < c.left+c.cw && y < c.top+c.ch) {
                assert(pixel == apply_filter(sample(c.x+x-c.left,c.y+y-c.top),Filter::Faded,x-c.left,y-c.top,c.cw,c.ch));
            } else assert(p != 0);
        }
        assert(save_photo(formats_root, chart, 972, 1024, Filter::Original, path, error, Aspect(a), Paper(p), p ? &art : nullptr));
        std::string named = formats_root + "/camera/photos/format-" + std::to_string(a) + "-" + std::to_string(p) + ".jpg";
        assert(rename(path.c_str(), named.c_str()) == 0);
        assert(load_photo(named, 568, 260, out, w, h, error));
    }
    // A new unframed 640x480 JPEG must never trigger legacy auto-rotation.
    std::vector<uint32_t> new_landscape(640*480, 0xffc05020);
    assert(save_photo(formats_root, new_landscape, 640, 480, Filter::Original, path, error));
    assert(load_photo(path, 544, 270, out, w, h, error) && w == 360 && h == 270);
    FrameArt missing;
    assert(!missing.load(root+"/absent", error) && !missing.ready());
    assert(!save_photo(formats_root,new_landscape,640,480,Filter::Original,path,error,Aspect::Landscape,Paper::White));
    puts("Shutter feedback timing, repeat guard, tick wrap, alpha blending and missing artwork tests passed");
    puts("All four crops and paper styles, preview/save sampling, and new landscape orientation passed");
    puts("Camera frame layout, color conversion, photo save and failure tests passed");
}
