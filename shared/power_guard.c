#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include "power_lock.h"

/*
 * Keep the vendor PowerManager awake for an active SSH workflow. When the
 * device has external power, also keep an idle Dropbear listener reachable so
 * a remote Terminal can connect after the screen has gone dark. On battery,
 * release the lock while idle so the stock suspend policy can save power.
 * PowerLock associates a suspend lock with this socket client, so a dead guard
 * cannot leave a permanent global lock behind.
 */
static volatile sig_atomic_t quitting;
static void stop_guard(int signal_number) { (void)signal_number; quitting = 1; }

static int read_number(const char *path) {
    FILE *file = fopen(path, "r");
    int value = -1;
    if (file) { if (fscanf(file, "%d", &value) != 1) value = -1; fclose(file); }
    return value;
}

static int process_info(pid_t pid, pid_t *parent, char *state, char *comm, size_t comm_size) {
    char path[64], line[512];
    snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
    FILE *file = fopen(path, "r");
    if (!file || !fgets(line, sizeof(line), file)) { if (file) fclose(file); return -1; }
    fclose(file);
    /* comm is enclosed in parentheses; Dropbear's name contains no ')'. */
    int parsed = sscanf(line, "%*d (%255[^)]) %c %d", comm, state, parent);
    if (parsed != 3) return -1;
    if (comm_size > 0) comm[comm_size - 1] = '\0';
    return 0;
}

static int listener_alive(pid_t listener) {
    pid_t parent = -1; char state = 0, name[256] = {0};
    if (listener <= 1 || process_info(listener, &parent, &state, name, sizeof(name)) != 0) return 0;
    return state != 'Z' && !strcmp(name, "dropbear");
}

static int active_session(pid_t listener) {
    DIR *directory = opendir("/proc");
    if (!directory) return 0;
    int active = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        char *end = NULL;
        long candidate = strtol(entry->d_name, &end, 10);
        if (!end || *end || candidate <= 1 || candidate == listener) continue;
        pid_t parent = -1; char state = 0, name[256] = {0};
        if (process_info((pid_t)candidate, &parent, &state, name, sizeof(name)) != 0 || state == 'Z') continue;
        if (parent == listener) { active = 1; break; }
        /* The normal Coding path is an outbound Dropbear client, so it has no
         * relationship to the local listener. Keep that connection awake too. */
        if (!strcmp(name, "dbclient") || !strcmp(name, "ssh") || !strcmp(name, "scp")) {
            active = 1; break;
        }
    }
    closedir(directory);
    return active;
}

static int externally_powered(void) {
    return read_number("/sys/class/power_supply/usb/online") > 0 ||
           read_number("/sys/class/power_supply/ac/online") > 0;
}

static int powerlock_connect(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, "/dev/socket/PowerLock", sizeof(address.sun_path) - 1);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) { close(fd); return -1; }
    return fd;
}

int main(void) {
    struct sigaction action;
    memset(&action, 0, sizeof(action)); action.sa_handler = stop_guard;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL); sigaction(SIGHUP, &action, NULL);
    signal(SIGPIPE, SIG_IGN);
    pid_t parent = getppid();
    if (parent <= 1 || prctl(PR_SET_PDEATHSIG, SIGTERM) || getppid() != parent) return 1;

    int lock_fd = -1;
    int heartbeat = 0, warned = 0;
    while (!quitting) {
        const pid_t listener = read_number("/storage/apps/data/terminal/dropbear/dropbear.pid");
        const int listening = listener_alive(listener);
        const int busy = active_session(listening ? listener : -1) ||
                         (listening && externally_powered());
        if (busy && lock_fd < 0) {
            lock_fd = powerlock_connect();
            if (lock_fd >= 0 && powerlock_command(lock_fd, "suslock") != 0) {
                if (!warned) fprintf(stderr, "[power-guard] suspend lock not acknowledged: %s; retrying\n", strerror(errno));
                warned = 1;
                close(lock_fd); lock_fd = -1;
            }
            if (lock_fd >= 0) {
                fprintf(stderr, "[power-guard] suspend lock acknowledged (pid %ld)\n", (long)getpid());
                warned = 0; heartbeat = 0;
            }
        } else if (!busy && lock_fd >= 0) {
            powerlock_command(lock_fd, "susunlock");
            close(lock_fd); lock_fd = -1;
            fprintf(stderr, "[power-guard] suspend lock released\n");
        } else if (lock_fd >= 0 && ++heartbeat >= 5) {
            heartbeat = 0;
            if (powerlock_command(lock_fd, "suslock") != 0) {
                close(lock_fd); lock_fd = -1;
                fprintf(stderr, "[power-guard] PowerLock acknowledgement lost; reconnecting\n");
            }
        }
        usleep(1000000);
    }
    if (lock_fd >= 0) { powerlock_command(lock_fd, "susunlock"); close(lock_fd); }
    return 0;
}
