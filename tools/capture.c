/* Capture the current fb2 scanout page. fb_read exposes only the first page
 * on this device; the read-only mmap exposes all three framebuffer pages.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { WIDTH = 340, HEIGHT = 800, ROW_BYTES = WIDTH * 4, RETRIES = 3 };

static int valid_view(const struct fb_var_screeninfo *v, const struct fb_fix_screeninfo *f) {
    if (v->xres != WIDTH || v->yres != HEIGHT || v->bits_per_pixel != 32 ||
        v->red.offset != 16 || v->green.offset != 8 || v->blue.offset != 0 ||
        v->red.length != 8 || v->green.length != 8 || v->blue.length != 8 ||
        (v->transp.length && (v->transp.length != 8 || v->transp.offset != 24)) ||
        f->line_length < ROW_BYTES || f->line_length % 4 || !f->smem_len ||
        f->smem_len > 64u * 1024u * 1024u ||
        (uint64_t)v->xoffset + WIDTH > v->xres_virtual ||
        (uint64_t)v->yoffset + HEIGHT > v->yres_virtual ||
        ((uint64_t)v->xoffset + WIDTH) * 4 > f->line_length) return 0;
    uint64_t end = ((uint64_t)v->yoffset + HEIGHT - 1) * f->line_length +
                   (uint64_t)v->xoffset * 4 + ROW_BYTES;
    return end <= f->smem_len;
}

static int same_view(const struct fb_var_screeninfo *a, const struct fb_var_screeninfo *b) {
    return a->xoffset == b->xoffset && a->yoffset == b->yoffset &&
           a->xres == b->xres && a->yres == b->yres &&
           a->xres_virtual == b->xres_virtual && a->yres_virtual == b->yres_virtual &&
           a->bits_per_pixel == b->bits_per_pixel &&
           a->red.offset == b->red.offset && a->green.offset == b->green.offset &&
           a->blue.offset == b->blue.offset && a->transp.offset == b->transp.offset &&
           a->red.length == b->red.length && a->green.length == b->green.length &&
           a->blue.length == b->blue.length && a->transp.length == b->transp.length;
}

static int save_frame(const char *path, const uint8_t *data, size_t length) {
    struct stat existing;
    if (lstat(path, &existing) == 0) {
        if (!S_ISREG(existing.st_mode)) { errno = EINVAL; return -1; }
    } else if (errno != ENOENT) return -1;
    char *temporary = NULL;
    if (asprintf(&temporary, "%s.tmp.XXXXXX", path) < 0) return -1;
    int out = mkstemp(temporary), result = -1, error;
    if (out < 0) { free(temporary); return -1; }
    for (size_t written = 0; written < length;) {
        ssize_t count = write(out, data + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) { if (!count) errno = EIO; goto done; }
        written += (size_t)count;
    }
    if (fsync(out) != 0) goto done;
    if (close(out) != 0) { out = -1; goto done; }
    out = -1;
    if (rename(temporary, path) != 0) goto done;
    result = 0;
done:
    error = errno;
    if (out >= 0) close(out);
    if (result) unlink(temporary);
    free(temporary);
    errno = error;
    return result;
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) { fprintf(stderr, "Usage: %s OUTPUT.raw [YOFFSET]\n", argv[0]); return 2; }
    int explicit_page = argc == 3;
    unsigned requested_offset = 0;
    if (explicit_page) {
        char *end;
        errno = 0;
        unsigned long parsed = strtoul(argv[2], &end, 10);
        if (errno || !*argv[2] || *end || argv[2][0] == '-' || parsed > UINT32_MAX) {
            fputs("capture: invalid YOFFSET\n", stderr); return 2;
        }
        requested_offset = (unsigned)parsed;
    }
    int fb = -1, result = 1;
    uint8_t *mapped = MAP_FAILED, *snapshot = NULL;
    struct fb_fix_screeninfo fixed, after_fixed;
    struct fb_var_screeninfo view, after;
    size_t mapped_length = 0;
    fb = open("/dev/fb2", O_RDONLY | O_CLOEXEC);
    if (fb < 0) { perror("capture: open fb2"); goto done; }
    if (ioctl(fb, FBIOGET_FSCREENINFO, &fixed) || ioctl(fb, FBIOGET_VSCREENINFO, &view)) {
        perror("capture: framebuffer info"); goto done;
    }
    if (explicit_page) view.yoffset = requested_offset;
    if (!valid_view(&view, &fixed)) { fputs("capture: unsupported framebuffer geometry, format or offset\n", stderr); goto done; }
    mapped_length = fixed.smem_len;
    mapped = mmap(NULL, mapped_length, PROT_READ, MAP_SHARED, fb, 0);
    if (mapped == MAP_FAILED) { perror("capture: read-only mmap"); goto done; }
    snapshot = malloc((size_t)ROW_BYTES * HEIGHT);
    if (!snapshot) { perror("capture: snapshot allocation"); goto done; }
    for (int attempt = 0; attempt < RETRIES; ++attempt) {
        if (ioctl(fb, FBIOGET_VSCREENINFO, &view)) {
            perror("capture: scanout info"); goto done;
        }
        if (explicit_page) view.yoffset = requested_offset;
        if (!valid_view(&view, &fixed)) {
            fputs("capture: framebuffer changed to an unsupported view\n", stderr); goto done;
        }
        for (unsigned row = 0; row < HEIGHT; ++row)
            memcpy(snapshot + (size_t)row * ROW_BYTES,
                   mapped + ((size_t)view.yoffset + row) * fixed.line_length + (size_t)view.xoffset * 4,
                   ROW_BYTES);
        if (ioctl(fb, FBIOGET_VSCREENINFO, &after) || ioctl(fb, FBIOGET_FSCREENINFO, &after_fixed)) {
            perror("capture: verify scanout"); goto done;
        }
        if (after_fixed.line_length != fixed.line_length || after_fixed.smem_len != fixed.smem_len) {
            fputs("capture: framebuffer storage changed during capture; retry\n", stderr); goto done;
        }
        if (explicit_page) after.yoffset = requested_offset;
        if (same_view(&view, &after)) {
            if (save_frame(argv[1], snapshot, (size_t)ROW_BYTES * HEIGHT)) {
                perror("capture: write snapshot"); goto done;
            }
            printf("CAPTURED bytes=%u size=%ux%u BGRA offset=%u,%u stride=%u\n",
                   ROW_BYTES * HEIGHT, WIDTH, HEIGHT, view.xoffset, view.yoffset, fixed.line_length);
            result = 0; goto done;
        }
        struct timespec delay = {.tv_nsec = 10000000};
        nanosleep(&delay, NULL);
    }
    fputs("capture: scanout changed during all three attempts; retry\n", stderr);
done:
    free(snapshot);
    if (mapped != MAP_FAILED) munmap(mapped, mapped_length);
    if (fb >= 0) close(fb);
    return result;
}
