/* Real Linux OSS playback through the QEMU AC97 sound card. */
#ifndef C1SIM_PCM_H
#define C1SIM_PCM_H
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <unistd.h>

static inline int pcm_open(int rate, int channels)
{
    int fd = open("/dev/dsp", O_WRONLY);
    if (fd < 0) return -1;
    int fragment = (4 << 16) | 10, format = AFMT_S16_LE;
    int actual_rate = rate, actual_channels = channels;
    if (ioctl(fd, SNDCTL_DSP_SETFRAGMENT, &fragment) < 0 ||
        ioctl(fd, SNDCTL_DSP_SETFMT, &format) < 0 ||
        ioctl(fd, SNDCTL_DSP_CHANNELS, &actual_channels) < 0 ||
        ioctl(fd, SNDCTL_DSP_SPEED, &actual_rate) < 0) {
        int error = errno; close(fd); errno = error; return -1;
    }
    if (format != AFMT_S16_LE || actual_rate != rate || actual_channels != channels) {
        close(fd); errno = EINVAL; return -1;
    }
    return fd;
}

static inline int pcm_write(int fd, void *data, size_t size, const volatile sig_atomic_t *stopped)
{
    /* Apply the original amixer value in the guest PCM path. AC97 voice
     * recreation can lose the host mixer gain; this also makes WAV/native
     * output obey the same volume and gives an exact mute at zero. */
    int volume = 158;
    int control = open("/run/c1sim-volume", O_RDONLY);
    if (control >= 0) {
        char value[16];
        ssize_t count = read(control, value, sizeof(value) - 1);
        close(control);
        if (count > 0) {
            value[count] = '\0';
            char *end;
            long raw = strtol(value, &end, 10);
            if (end != value && raw >= 0 && raw <= 158) volume = (int)raw;
        }
    }
    if (volume != 158) {
        unsigned char *samples = data;
        for (size_t i = 0; i + 1 < size; i += 2) {
            int sample = (int16_t)((unsigned int)samples[i] | ((unsigned int)samples[i + 1] << 8));
            sample = sample * volume / 158;
            samples[i] = (unsigned char)sample;
            samples[i + 1] = (unsigned char)((unsigned int)sample >> 8);
        }
    }
    const unsigned char *bytes = data;
    while (size && !*stopped) {
        ssize_t written = write(fd, bytes, size);
        if (written < 0) { if (errno == EINTR) continue; return -1; }
        if (!written) { errno = EIO; return -1; }
        bytes += written; size -= (size_t)written;
    }
    if (*stopped) { errno = EINTR; return -1; }
    return 0;
}

static inline int pcm_finish(int fd, int discard)
{
    int result = ioctl(fd, discard ? SNDCTL_DSP_RESET : SNDCTL_DSP_SYNC, 0);
    int error = errno;
    close(fd); errno = error;
    return result;
}
#endif
