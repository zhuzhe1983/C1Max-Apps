/* Read-only global shortcuts, both firing AT the hold threshold (no release
 * needed): Shift+Enter held 1.5s, or Back (event1 only) held 2s. Each fires
 * once per press and re-arms after the chord/key is released.
 * No EVIOCGRAB and no synthesized input: the stock UI keeps ordinary keys.
 * The independent launcher supervisor survives this daemon stopping/restarting.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#include "../../shared/mute_hold.h"

#define HOLD_MS 1500u
#define BACK_HOLD_MS 2000u
enum key_slot { LEFT_SHIFT, RIGHT_SHIFT, ENTER, KP_ENTER, BACK, SLOT_COUNT };
struct hotkey_state {
    unsigned char keys[2][SLOT_COUNT];
    int timing, armed, inhibited;
    uint64_t since;
    uint64_t back_since;
    int back_fired;
};
static int any_key(const struct hotkey_state *state) {
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < SLOT_COUNT; ++j) if (state->keys[i][j]) return 1;
    return 0;
}
static int chord_down(const struct hotkey_state *state) {
    int shift = 0, enter = 0;
    for (int i = 0; i < 2; ++i) {
        shift |= state->keys[i][LEFT_SHIFT] | state->keys[i][RIGHT_SHIFT];
        enter |= state->keys[i][ENTER] | state->keys[i][KP_ENTER];
    }
    return shift && enter;
}
static void inhibit(struct hotkey_state *state) {
    state->timing = state->armed = 0;
    state->back_since = 0;          /* A pending back-hold must start over. */
    state->back_fired = 1;          /* Suppress until the key is fully released. */
    state->inhibited = 1;
}
/* The physical Back key lives on event1; ignore code 14 on the keypad device. */
static int back_held(const struct hotkey_state *state) {
    return state->keys[1][BACK];
}
static int back_ready(const struct hotkey_state *state, uint64_t now) {
    return !state->inhibited && !state->back_fired && back_held(state) &&
           state->back_since && now >= state->back_since &&
           now - state->back_since >= BACK_HOLD_MS;
}
static void maybe_arm(struct hotkey_state *state, uint64_t now) {
    if (!state->inhibited && !state->armed && state->timing && chord_down(state) &&
        now >= state->since && now - state->since >= HOLD_MS) state->armed = 1;
}
/* armed = already fired for this press; releasing the chord re-arms it. */
static int chord_ready(struct hotkey_state *state, uint64_t now) {
    int was = state->armed;
    maybe_arm(state, now);
    return !was && state->armed;
}
static void key_update(struct hotkey_state *state, int device, int slot,
                       int value, uint64_t now) {
    if (device < 0 || device > 1 || slot < 0 || slot >= SLOT_COUNT ||
        (value != 0 && value != 1)) return;  /* Auto-repeat never starts a hold. */
    state->keys[device][slot] = value != 0;
    if (state->inhibited) {
        if (!any_key(state)) state->inhibited = 0;
        return;
    }
    if (!chord_down(state)) { state->timing = 0; state->armed = 0; }
    if (state->armed) return;  /* Fired; ignore further presses until release. */
    if (slot == BACK && device == 1) {
        if (value) { state->back_since = now; state->back_fired = 0; }
        else state->back_since = 0;
    }
    if (chord_down(state) && !state->timing) { state->timing = 1; state->since = now; }
}
/* Clear stale startup/resync inhibition once nothing is physically held;
 * otherwise the first gesture after a foreground->stock switch is swallowed. */
static void maybe_uninhibit(struct hotkey_state *state) {
    if (state->inhibited && !any_key(state)) state->inhibited = 0;
}
static int foreground_busy(const char *path) {
    /* Use the same persistent inode as run.sh. A stale run.lock directory is
     * intentionally left to the supervisor's boot-ID/identity recovery. */
    int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return 1;
    if (flock(fd, LOCK_EX | LOCK_NB)) { close(fd); return 1; }
    int failed = flock(fd, LOCK_UN);
    close(fd);
    return failed != 0;
}

#ifndef C1_HOTKEY_UNIT_TEST
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <tinyalsa/mixer.h>

static void mute_system(void) {
    struct mixer *m=mixer_open(0);
    struct mixer_ctl *c=m?mixer_get_ctl_by_name(m,"softvolume"):NULL;
    if(c&&mixer_ctl_get_type(c)==MIXER_CTL_TYPE_INT&&mixer_ctl_get_num_values(c)==2){
        long low=mixer_ctl_get_range_min(c),levels[2]={low,low};
        if(mixer_ctl_set_array(c,levels,2)==0)fprintf(stderr,"[hotkey] Volume down held 3s: system muted\n");
        else fprintf(stderr,"[hotkey] Could not mute system volume\n");
    }else fprintf(stderr,"[hotkey] System volume control unavailable\n");
    if(m)mixer_close(m);
}

static volatile sig_atomic_t quitting;
static void stop(int signal_number) { (void)signal_number; quitting = 1; }
static int clock_ms(uint64_t *value) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    *value = (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
    return 0;
}
static int slot_for_code(unsigned code) {
    switch (code) {
        case KEY_LEFTSHIFT: return LEFT_SHIFT;
        case KEY_RIGHTSHIFT: return RIGHT_SHIFT;
        case KEY_ENTER: return ENTER;
        case KEY_KPENTER: return KP_ENTER;
        case KEY_BACKSPACE: return BACK;  /* Physical Back key (event1). */
        default: return -1;
    }
}
static int make_path(char *out, size_t size, const char *base, const char *suffix) {
    if (!base || base[0] != '/') { errno = EINVAL; return -1; }
    int length = snprintf(out, size, "%s%s", base, suffix);
    if (length < 0 || (size_t)length >= size) { errno = ENAMETOOLONG; return -1; }
    return 0;
}
static int make_directories(const char *path) {
    char current[PATH_MAX];
    if (strlen(path) >= sizeof current) { errno = ENAMETOOLONG; return -1; }
    strcpy(current, path);
    for (char *p = current + 1; ; ++p) {
        if (*p != '/' && *p) continue;
        char saved = *p;
        *p = '\0';
        if (mkdir(current, 0700) && errno != EEXIST) return -1;
        *p = saved;
        if (!saved) return 0;
    }
}
static int path_exists(const char *path) {
    struct stat st;
    if (lstat(path, &st) == 0) return 1;
    /* Fail closed when permissions or a transient filesystem error obscure it. */
    return errno != ENOENT;
}
static void resync_keys(struct hotkey_state *state, int index, int fd) {
    unsigned char bits[(KEY_MAX + 8) / 8];
    memset(bits, 0, sizeof bits);
    memset(state->keys[index], 0, sizeof state->keys[index]);
    if (ioctl(fd, EVIOCGKEY(sizeof bits), bits) >= 0) {
        static const unsigned codes[] = {KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_ENTER, KEY_KPENTER, KEY_BACKSPACE};
        for (int i = 0; i < SLOT_COUNT; ++i)
            state->keys[index][i] = !!(bits[codes[i] / 8] & (1u << (codes[i] % 8)));
    }
    /* Do not reinterpret already-held keys or dropped events as a new chord. */
    inhibit(state);
}
static pid_t launch(const char *script, const char *log_path,
                    const struct pollfd *inputs, int lock_fd) {
    if (access(script, X_OK)) { perror("[hotkey] Launcher supervisor unavailable"); return -1; }
    pid_t child = fork();
    if (child != 0) return child;
    signal(SIGTERM, SIG_DFL); signal(SIGINT, SIG_DFL); signal(SIGHUP, SIG_DFL);
    if (setsid() < 0) _exit(126);
    for (int i = 0; i < 2; ++i) if (inputs[i].fd >= 0) close(inputs[i].fd);
    close(lock_fd);
    int input = open("/dev/null", O_RDONLY);
    int output = open(log_path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
    if (input < 0 || output < 0) _exit(126);
    if (dup2(input, STDIN_FILENO) < 0 || dup2(output, STDOUT_FILENO) < 0 ||
        dup2(output, STDERR_FILENO) < 0) _exit(126);
    if (input > STDERR_FILENO) close(input);
    if (output > STDERR_FILENO) close(output);
    /* No PDEATHSIG: stopping/restarting the hotkey daemon must not kill apps. */
    execl(script, script, (char *)NULL);
    perror("[hotkey] exec launcher supervisor");
    _exit(127);
}
int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "--version")) {
        puts("C1Max hotkey 0.4.0 (Shift+Enter 1500ms; Back 2000ms; Volume- mute 3000ms; fire at threshold)"); return 0;
    }
    if (argc != 1) { fprintf(stderr, "Usage: %s [--version]\n", argv[0]); return 2; }
    const char *root = getenv("C1_APPS_ROOT"), *data = getenv("C1_APPS_DATA");
    if (!root || !*root) root = "/storage/apps/current";
    if (!data || !*data) data = "/storage/apps/data";
    char script[PATH_MAX], state_dir[PATH_MAX], foreground_lock[PATH_MAX];
    char daemon_lock[PATH_MAX], log_path[PATH_MAX], disabled_path[PATH_MAX];
    if (make_path(script, sizeof script, root, "/launcher/run.sh") ||
        make_path(state_dir, sizeof state_dir, data, "/launcher") ||
        make_path(foreground_lock, sizeof foreground_lock, state_dir, "/foreground.lock") ||
        make_path(daemon_lock, sizeof daemon_lock, state_dir, "/hotkey.lock") ||
        make_path(disabled_path, sizeof disabled_path, state_dir, "/hotkey.disabled") ||
        make_path(log_path, sizeof log_path, state_dir, "/hotkey-launch.log") ||
        make_directories(state_dir)) { perror("[hotkey] Paths"); return 1; }
    int lock_fd = open(daemon_lock, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_fd < 0) { perror("[hotkey] Open instance lock"); return 1; }
    if (flock(lock_fd, LOCK_EX | LOCK_NB)) {
        int busy = errno == EWOULDBLOCK || errno == EAGAIN;
        if (!busy) perror("[hotkey] Lock instance");
        close(lock_fd); return busy ? 0 : 1;
    }
    if (ftruncate(lock_fd, 0) || dprintf(lock_fd, "%ld\n", (long)getpid()) < 0) {
        perror("[hotkey] Write instance lock"); close(lock_fd); return 1;
    }
    struct sigaction action;
    memset(&action, 0, sizeof action); action.sa_handler = stop;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL);
    sigaction(SIGHUP, &action, NULL);
    static const char *devices[] = {"/dev/input/event0", "/dev/input/event1"};
    struct pollfd inputs[2] = {{.fd = -1, .events = POLLIN}, {.fd = -1, .events = POLLIN}};
    struct hotkey_state state = {0};
    struct c1_mute_hold mute = {0};
    int dropped[2] = {0}, active = 0, result = 1;
    pid_t child = -1;
    for (int i = 0; i < 2; ++i) {
        inputs[i].fd = open(devices[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (inputs[i].fd < 0) { fprintf(stderr, "[hotkey] %s: %s\n", devices[i], strerror(errno)); continue; }
        ++active;
        resync_keys(&state, i, inputs[i].fd);
    }
    if (!active) goto done;
    fprintf(stderr, "[hotkey] Ready: hold Shift+Enter 1.5s or Back 2s (fires at the threshold); input remains shared\n");
    while (!quitting && active) {
        uint64_t now;
        if (clock_ms(&now)) { perror("[hotkey] Clock"); goto done; }
        if(c1_mute_due(&mute,now))mute_system();
        if (child > 0) {
            int status;
            pid_t reaped = waitpid(child, &status, WNOHANG);
            if (reaped == child || (reaped < 0 && errno == ECHILD)) child = -1;
        }
        if (child > 0 || foreground_busy(foreground_lock) || path_exists(disabled_path)) inhibit(&state);
        else maybe_uninhibit(&state);
        if (chord_ready(&state, now) && !quitting) {
            /* Once per chord press; re-arms after the keys are released. */
            if (!foreground_busy(foreground_lock) && !path_exists(disabled_path) && child <= 0) {
                child = launch(script, log_path, inputs, lock_fd);
                if (child < 0) perror("[hotkey] Launch");
                else fprintf(stderr, "[hotkey] Started independent supervisor PID %ld\n", (long)child);
            }
        }
        if (back_ready(&state, now) && !quitting) {
            /* Once per press: fires at the threshold, no need to release. */
            state.back_fired = 1;
            if (!foreground_busy(foreground_lock) && !path_exists(disabled_path) && child <= 0) {
                child = launch(script, log_path, inputs, lock_fd);
                if (child < 0) perror("[hotkey] Launch");
                else fprintf(stderr, "[hotkey] Back held 2s: started independent supervisor PID %ld\n", (long)child);
            }
        }
        int timeout = 1000;
        if (state.timing && !state.armed && !state.inhibited) {
            uint64_t elapsed = now >= state.since ? now - state.since : 0;
            timeout = elapsed >= HOLD_MS ? 0 : (int)(HOLD_MS - elapsed);
            if (timeout > 1000) timeout = 1000;
        }
        if (!state.inhibited && !state.back_fired && back_held(&state) && state.back_since) {
            uint64_t elapsed = now >= state.back_since ? now - state.back_since : 0;
            int remaining = elapsed >= BACK_HOLD_MS ? 0 : (int)(BACK_HOLD_MS - elapsed);
            if (remaining < timeout) timeout = remaining;
        }
        int count = poll(inputs, 2, c1_mute_timeout(&mute,now,timeout));
        if (count < 0) { if (errno == EINTR) continue; perror("[hotkey] poll"); goto done; }
        for (int i = 0; i < 2 && !quitting; ++i) {
            if (inputs[i].fd < 0 || !inputs[i].revents) continue;
            if (inputs[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "[hotkey] Disconnected: %s\n", devices[i]);
                close(inputs[i].fd); inputs[i].fd = -1; --active;
                memset(state.keys[i], 0, sizeof state.keys[i]); inhibit(&state);
                c1_mute_reset(&mute);
                continue;
            }
            struct input_event event;
            ssize_t bytes;
            while (!quitting && (bytes = read(inputs[i].fd, &event, sizeof event)) == sizeof event) {
                if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                    dropped[i] = 1; inhibit(&state); c1_mute_reset(&mute); continue;
                }
                if (dropped[i]) {
                    if (event.type == EV_SYN && event.code == SYN_REPORT) {
                        dropped[i] = 0; resync_keys(&state, i, inputs[i].fd);
                    }
                    continue;
                }
                if (event.type != EV_KEY) continue;
                if (clock_ms(&now)) { perror("[hotkey] Clock"); goto done; }
                if(c1_mute_due(&mute,now))mute_system();
                key_update(&state, i, slot_for_code(event.code), event.value, now);
                int shifted=state.keys[0][LEFT_SHIFT]||state.keys[0][RIGHT_SHIFT]||state.keys[1][LEFT_SHIFT]||state.keys[1][RIGHT_SHIFT];
                if(shifted&&mute.down)mute.blocked=1;
                if(event.code==KEY_VOLUMEDOWN)c1_mute_key(&mute,i,event.value,now,shifted);
            }
            if (!quitting && (bytes == 0 || (bytes > 0 && bytes != sizeof event) ||
                (bytes < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))) {
                fprintf(stderr, "[hotkey] Cannot read complete events from %s\n", devices[i]); goto done;
            }
        }
    }
    result = quitting ? 0 : 1;
done:
    for (int i = 0; i < 2; ++i) if (inputs[i].fd >= 0) close(inputs[i].fd);
    close(lock_fd);
    /* Kernel releases flock. Do not unlink its inode or signal our child. */
    return result;
}
#endif
