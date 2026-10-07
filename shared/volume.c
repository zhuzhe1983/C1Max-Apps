/* Session-wide C1 Max volume keys. The stock smartUI handler is stopped while
 * our apps own the foreground, so one helper handles volume for every app.
 * No EVIOCGRAB: apps still receive their normal keyboard events.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>
#include <tinyalsa/mixer.h>
#include "idle_reset.h"

#define VOLUME_STEP 13
#define REPEAT_MS 150

static volatile sig_atomic_t quitting;
static void stop(int signal_number) { (void)signal_number; quitting = 1; }
static int app_font_shortcuts(const char *marker,const char *expected) {
    FILE *f=fopen(marker,"r");long pid=0;
    if(!f)return 0;int valid=fscanf(f,"%ld",&pid)==1;fclose(f);
    if(!valid||pid<=1)return 0;
    char path[80],name[32]={0};snprintf(path,sizeof(path),"/proc/%ld/comm",pid);
    f=fopen(path,"r");if(!f)return 0;valid=fgets(name,sizeof(name),f)!=NULL;fclose(f);
    return valid&&!strcmp(name,expected);
}
static int font_shortcuts(void) {
    static const char *ime_apps[]={"c1max-airtune\n","c1max-streampla\n","c1max-bilibili\n",
        "c1max-calendar\n","c1max-mail\n","c1max-settings\n","c1max-moonpilot\n",
        "c1max-crosspoin\n","c1max-tox\n"};
    for(size_t i=0;i<sizeof(ime_apps)/sizeof(ime_apps[0]);i++)
        if(app_font_shortcuts("/tmp/c1max-ime.pid",ime_apps[i]))return 1;
    return app_font_shortcuts("/tmp/c1max-terminal-font.pid","c1max-terminal\n") ||
           app_font_shortcuts("/tmp/c1max-crosspoint-font.pid","c1max-crosspoin\n") ||
           app_font_shortcuts("/tmp/c1max-tox-scroll.pid","c1max-tox\n");
}

static uint64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return 0;
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

/* Level chosen in Settings (1..15); full brightness when never set. */
static int saved_backlight(void) {
    const char *data = getenv("C1_APPS_DATA");
    char path[160], text[8] = {0};
    snprintf(path, sizeof(path), "%s/settings/brightness", data && *data ? data : "/storage/apps/data");
    int fd = open(path, O_RDONLY | O_CLOEXEC), level = 15;
    if (fd < 0) return level;
    if (read(fd, text, sizeof(text) - 1) > 0) level = atoi(text);
    close(fd);
    return level >= 1 && level <= 15 ? level : 15;
}

static void wake_backlight(void) {
    int fd = open("/sys/class/backlight/backlight/actual_brightness", O_RDONLY | O_CLOEXEC);
    char cur[8] = {0}, level[8];
    if (fd < 0) return;
    ssize_t n = read(fd, cur, sizeof(cur) - 1);
    close(fd);
    if (n <= 0 || cur[0] != '0') return;
    fd = open("/sys/class/backlight/backlight/brightness", O_WRONLY | O_CLOEXEC);
    if (fd < 0) return;
    int len = snprintf(level, sizeof(level), "%d\n", saved_backlight());
    if (write(fd, level, len) == len) fprintf(stderr, "[volume] backlight wake\n");
    close(fd);
}

static long stepped(long value, int delta, int minimum, int maximum) {
    int64_t next = (int64_t)value + delta;
    if (next < minimum) next = minimum;
    if (next > maximum) next = maximum;
    return (long)next;
}

static int adjust(struct mixer_ctl *control, int delta) {
    /* tinyalsa integer arrays contain native long values, not int on LP64. */
    long values[2];
    int minimum = mixer_ctl_get_range_min(control);
    int maximum = mixer_ctl_get_range_max(control);
    if (minimum >= maximum || mixer_ctl_get_array(control, values, 2)) {
        fprintf(stderr, "[volume] Cannot read softvolume/range\n");
        return -1;
    }
    values[0] = stepped(values[0], delta, minimum, maximum);
    values[1] = stepped(values[1], delta, minimum, maximum);
    if (mixer_ctl_set_array(control, values, 2)) {
        fprintf(stderr, "[volume] Cannot write softvolume\n");
        return -1;
    }
    fprintf(stderr, "[volume] softvolume=%ld,%ld (range %d..%d)\n",
            values[0], values[1], minimum, maximum);
    return 0;
}

int main(void) {
    static const char *devices[] = {"/dev/input/event0", "/dev/input/event1", "/dev/input/event2"};
    struct pollfd inputs[3] = {{.fd = -1}, {.fd = -1}, {.fd = -1}};
    struct mixer *mixer = NULL;
    struct mixer_ctl *control;
    uint64_t last_action[2] = {0, 0};
    int discarded[3] = {0, 0, 0};
    uint64_t last_idle_reset=0;
    int shift[2] = {0,0};
    int active = 0, result = 1;
    pid_t parent = getppid();
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    if (parent <= 1 || prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent) {
        fprintf(stderr, "[volume] Supervisor is unavailable\n");
        goto done;
    }

    mixer = mixer_open(0);
    control = mixer ? mixer_get_ctl_by_name(mixer, "softvolume") : NULL;
    if (!control || mixer_ctl_get_type(control) != MIXER_CTL_TYPE_INT ||
        mixer_ctl_get_num_values(control) != 2) {
        fprintf(stderr, "[volume] Missing stereo integer softvolume on card 0\n");
        goto done;
    }
    /* Verify readability without changing the user's initial volume. */
    {
        long initial[2];
        if (mixer_ctl_get_array(control, initial, 2) ||
            mixer_ctl_get_range_min(control) >= mixer_ctl_get_range_max(control)) {
            fprintf(stderr, "[volume] softvolume is not readable\n");
            goto done;
        }
    }
    for (int i = 0; i < 3; ++i) {
        inputs[i].fd = open(devices[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        inputs[i].events = POLLIN;
        if (inputs[i].fd < 0) {
            fprintf(stderr, "[volume] %s: %s\n", devices[i], strerror(errno));
            continue;
        }
        ++active;
        /* Discard keys queued during handoff from the original desktop. */
        struct input_event stale;
        while (read(inputs[i].fd, &stale, sizeof(stale)) == sizeof(stale)) {}
    }
    /* event0 contains the physical volume keys on this hardware. */
    if (inputs[0].fd < 0) goto done;
    c1_reset_idle();last_idle_reset=milliseconds();
    fprintf(stderr, "[volume] Listening on event0/1/2; idle notifications active; step=%d, repeat=%dms\n",
            VOLUME_STEP, REPEAT_MS);
    while (!quitting && active) {
        int ready = poll(inputs, 3, 500);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("[volume] poll");
            goto done;
        }
        for (int i = 0; i < 3 && !quitting; ++i) {
            if (inputs[i].fd < 0 || !inputs[i].revents) continue;
            if (inputs[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "[volume] Input device disconnected: %s\n", devices[i]);
                close(inputs[i].fd); inputs[i].fd = -1; --active;
                if (i == 0) goto done;
                continue;
            }
            struct input_event event;
            ssize_t count;
            while (!quitting && (count = read(inputs[i].fd, &event, sizeof(event))) == sizeof(event)) {
                if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                    discarded[i] = 1;
                    shift[0]=shift[1]=0;
                    continue;
                }
                if (discarded[i]) {
                    if (event.type == EV_SYN && event.code == SYN_REPORT) discarded[i] = 0;
                    continue;
                }
                if ((event.type==EV_KEY&&event.value>0) || event.type==EV_ABS) {
                    uint64_t now=milliseconds();
                    if(now-last_idle_reset>=1000){c1_reset_idle();last_idle_reset=now;}
                    wake_backlight();
                }
                if(i==2)continue; /* Touch only refreshes the stock idle timer. */
                if(event.type==EV_KEY&&(event.code==KEY_LEFTSHIFT||event.code==KEY_RIGHTSHIFT)){
                    shift[event.code==KEY_RIGHTSHIFT]=event.value!=0;continue;
                }
                if (event.type != EV_KEY || (event.value != 1 && event.value != 2)) continue;
                int direction;
                if (event.code == KEY_VOLUMEUP) direction = 1;
                else if (event.code == KEY_VOLUMEDOWN) direction = 0;
                else continue;
                if((shift[0]||shift[1])&&font_shortcuts())continue;
                uint64_t now = milliseconds();
                if (last_action[direction] && now - last_action[direction] < REPEAT_MS) continue;
                last_action[direction] = now;
                if (adjust(control, direction ? VOLUME_STEP : -VOLUME_STEP)) goto done;
            }
            if (!quitting && count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                perror("[volume] read input");
                goto done;
            }
        }
    }
    result = quitting ? 0 : 1;
done:
    for (int i = 0; i < 3; ++i) if (inputs[i].fd >= 0) close(inputs[i].fd);
    c1_reset_idle(); /* Restored stock UI receives a fresh idle interval. */
    if (mixer) mixer_close(mixer);
    return result;
}
