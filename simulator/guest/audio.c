#define _POSIX_C_SOURCE 200809L
/* The original applications' amixer/aplay subset, backed by Linux OSS. */
#include "pcm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }

static int mixer_write(unsigned long control, int percent)
{
    int device = open("/dev/mixer", O_RDWR);
    if (device < 0) { perror("AC97 mixer"); return 1; }
    int value = percent | (percent << 8);
    int result = ioctl(device, control, &value);
    if (result < 0) perror("AC97 volume");
    close(device);
    return result < 0;
}

static int mixer(int argc, char **argv)
{
    if (argc != 5 || strcmp(argv[1], "-q") || strcmp(argv[2], "cset") ||
        strcmp(argv[3], "numid=1,iface=MIXER,name=DAC Playback Volume")) {
        fprintf(stderr, "c1sim amixer: unsupported mixer control\n"); return 1;
    }
    char *end;
    long volume = strtol(argv[4], &end, 10);
    if (end == argv[4] || *end || volume < 0 || volume > 158) return 1;
    FILE *file = fopen("/run/c1sim-volume.new", "w");
    if (!file) { perror("mixer state"); return 1; }
    fprintf(file, "%ld\n", volume);
    if (fclose(file) || rename("/run/c1sim-volume.new", "/run/c1sim-volume")) return 1;
    return 0;
}

static int play(int argc, char **argv)
{
    int rate = 48000, channels = 2;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-q")) continue;
        if (i + 1 >= argc) return 1;
        char *option = argv[i++], *value = argv[i];
        if (!strcmp(option, "-r")) rate = atoi(value);
        else if (!strcmp(option, "-c")) channels = atoi(value);
        else if (!strcmp(option, "-f") && strcmp(value, "S16_LE")) return 1;
        else if (!strcmp(option, "-t") && strcmp(value, "raw")) return 1;
        else if (strcmp(option, "-D") && strcmp(option, "-B") && strcmp(option, "-R") &&
                 strcmp(option, "-T") && strcmp(option, "-f") && strcmp(option, "-t")) {
            fprintf(stderr, "c1sim aplay: unsupported argument %s\n", option); return 1;
        }
    }
    if (rate < 8000 || rate > 192000 || channels < 1 || channels > 2) return 1;
    int device = pcm_open(rate, channels);
    if (device < 0) { perror("AC97 PCM"); return 1; }
    unsigned char data[4096];
    int result = 0;
    while (!stopped) {
        ssize_t size = read(STDIN_FILENO, data, sizeof(data));
        if (!size) break;
        if (size < 0) { if (errno == EINTR) continue; perror("PCM input"); result = 1; break; }
        if (pcm_write(device, data, (size_t)size, &stopped) < 0) {
            if (!stopped) { perror("PCM output"); result = 1; }
            break;
        }
    }
    if (pcm_finish(device, stopped || result) < 0 && !stopped) { perror("PCM drain"); result = 1; }
    return result;
}

int main(int argc, char **argv)
{
    struct sigaction action = { .sa_handler = stop };
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL);
    const char *name = strrchr(argv[0], '/'); name = name ? name + 1 : argv[0];
    if (!strcmp(name, "amixer")) return mixer(argc, argv);
    if (argc == 2 && !strcmp(argv[1], "--init"))
        return mixer_write(SOUND_MIXER_WRITE_VOLUME, 100) || mixer_write(SOUND_MIXER_WRITE_PCM, 100);
    return play(argc, argv);
}
