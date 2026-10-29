/* Own MIPS test fixture: continuously change a real guest framebuffer at 60 Hz. */
#define _GNU_SOURCE
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

int main(void)
{
    int fd = open("/dev/fb2", O_RDWR);
    struct fb_fix_screeninfo fixed;
    struct fb_var_screeninfo view;
    if (fd < 0 || ioctl(fd, FBIOGET_FSCREENINFO, &fixed) || ioctl(fd, FBIOGET_VSCREENINFO, &view)) return 1;
    uint32_t *memory = mmap(NULL, fixed.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (memory == MAP_FAILED) return 1;
    view.yoffset = 0;
    if (ioctl(fd, FBIOPAN_DISPLAY, &view)) return 1;
    struct timespec next; clock_gettime(CLOCK_MONOTONIC, &next);
    for (unsigned frame = 0; frame < 600; ++frame) {
        for (unsigned i = 0; i < 340 * 800; ++i) memory[i] = 0xff000000u | (frame * 7919u & 0xffffffu);
        next.tv_nsec += 1000000000 / 60;
        if (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; ++next.tv_sec; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    puts("FRAME_PROBE source_frames=600");
    munmap(memory, fixed.smem_len); close(fd); return 0;
}
