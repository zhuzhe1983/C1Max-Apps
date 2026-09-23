/* Read-only global Shift+Enter shortcut. Hold 1.5s, then release BOTH keys.
 * No EVIOCGRAB and no synthesized input: the stock UI keeps ordinary keys.
 * The independent launcher supervisor survives this daemon stopping/restarting.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#define HOLD_MS 1500u
enum key_slot { LEFT_SHIFT, RIGHT_SHIFT, ENTER, KP_ENTER, SLOT_COUNT };
struct hotkey_state {
    unsigned char keys[2][SLOT_COUNT];
    int timing, armed, inhibited;
    uint64_t since;
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
    state->inhibited = 1;
}
static void maybe_arm(struct hotkey_state *state, uint64_t now) {
    if (!state->inhibited && state->timing && chord_down(state) &&
        now >= state->since && now - state->since >= HOLD_MS) state->armed = 1;
}
static void key_update(struct hotkey_state *state, int device, int slot,
                       int value, uint64_t now) {
    if (device < 0 || device > 1 || slot < 0 || slot >= SLOT_COUNT ||
        (value != 0 && value != 1)) return;  /* Auto-repeat never starts a hold. */
    /* A release delivered after a delayed poll still counts the held duration. */
    maybe_arm(state, now);
    state->keys[device][slot] = value != 0;
    if (state->inhibited) {
        if (!any_key(state)) state->inhibited = 0;
        return;
    }
    if (state->armed) return;
    if (chord_down(state)) {
        if (!state->timing) { state->timing = 1; state->since = now; }
    } else state->timing = 0;
}
static int ready_after_release(struct hotkey_state *state, uint64_t now) {
    if (state->inhibited) {
        if (!any_key(state)) state->inhibited = 0;
        return 0;
    }
    maybe_arm(state, now);
    if (state->armed && !any_key(state)) {
        state->armed = state->timing = 0;
        return 1;
    }
    return 0;
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
        static const unsigned codes[] = {KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_ENTER, KEY_KPENTER};
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
        puts("C1Max hotkey 0.1.0 (Shift+Enter: hold 1500ms, then release)"); return 0;
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
    int dropped[2] = {0}, active = 0, result = 1;
    pid_t child = -1;
    for (int i = 0; i < 2; ++i) {
        inputs[i].fd = open(devices[i], O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (inputs[i].fd < 0) { fprintf(stderr, "[hotkey] %s: %s\n", devices[i], strerror(errno)); continue; }
        ++active;
        resync_keys(&state, i, inputs[i].fd);
    }
    if (!active) goto done;
    fprintf(stderr, "[hotkey] Ready: hold Shift+Enter 1.5s, then release both; input remains shared\n");
    while (!quitting && active) {
        uint64_t now;
        if (clock_ms(&now)) { perror("[hotkey] Clock"); goto done; }
        if (child > 0) {
            int status;
            pid_t reaped = waitpid(child, &status, WNOHANG);
            if (reaped == child || (reaped < 0 && errno == ECHILD)) child = -1;
        }
        if (child > 0 || foreground_busy(foreground_lock) || path_exists(disabled_path)) inhibit(&state);
        if (ready_after_release(&state, now) && !quitting) {
            /* Recheck the lock at the actual handoff, after both keys are up. */
            if (!foreground_busy(foreground_lock) && !path_exists(disabled_path) && child <= 0) {
                child = launch(script, log_path, inputs, lock_fd);
                if (child < 0) perror("[hotkey] Launch");
                else fprintf(stderr, "[hotkey] Started independent supervisor PID %ld\n", (long)child);
            }
        }
        int timeout = 1000;
        if (state.timing && !state.armed && !state.inhibited) {
            uint64_t elapsed = now >= state.since ? now - state.since : 0;
            timeout = elapsed >= HOLD_MS ? 0 : (int)(HOLD_MS - elapsed);
            if (timeout > 1000) timeout = 1000;
        }
        int count = poll(inputs, 2, timeout);
        if (count < 0) { if (errno == EINTR) continue; perror("[hotkey] poll"); goto done; }
        for (int i = 0; i < 2 && !quitting; ++i) {
            if (inputs[i].fd < 0 || !inputs[i].revents) continue;
            if (inputs[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "[hotkey] Disconnected: %s\n", devices[i]);
                close(inputs[i].fd); inputs[i].fd = -1; --active;
                memset(state.keys[i], 0, sizeof state.keys[i]); inhibit(&state);
                continue;
            }
            struct input_event event;
            ssize_t bytes;
            while (!quitting && (bytes = read(inputs[i].fd, &event, sizeof event)) == sizeof event) {
                if (event.type == EV_SYN && event.code == SYN_DROPPED) {
                    dropped[i] = 1; inhibit(&state); continue;
                }
                if (dropped[i]) {
                    if (event.type == EV_SYN && event.code == SYN_REPORT) {
                        dropped[i] = 0; resync_keys(&state, i, inputs[i].fd);
                    }
                    continue;
                }
                if (event.type != EV_KEY) continue;
                if (clock_ms(&now)) { perror("[hotkey] Clock"); goto done; }
                key_update(&state, i, slot_for_code(event.code), event.value, now);
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
