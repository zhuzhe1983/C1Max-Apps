#ifndef C1_POWER_LOCK_H
#define C1_POWER_LOCK_H

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static long long powerlock_milliseconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

/* Vendor libsysutils dispatches NUL-terminated FrameworkListener commands.
 * PowerManager identifies each registration by PID and replies with "ok\0".
 * A successful write alone does not mean a suspend lock was acquired. */
static int powerlock_command(int fd, const char *command) {
    char frame[96], reply[64];
    int length = snprintf(frame, sizeof(frame), "Register %s %ld", command, (long)getpid());
    if (length < 0 || length >= (int)sizeof(frame)) { errno = EINVAL; return -1; }
    const long long deadline = powerlock_milliseconds() + 1000;
    int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
    flags |= MSG_NOSIGNAL;
#endif
    for (size_t offset = 0; offset < (size_t)length + 1;) {
        long long remaining = deadline - powerlock_milliseconds();
        if (remaining <= 0) { errno = ETIMEDOUT; return -1; }
        struct pollfd descriptor = {.fd = fd, .events = POLLOUT};
        int ready = poll(&descriptor, 1, (int)remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { if (!ready) errno = ETIMEDOUT; return -1; }
        ssize_t sent = send(fd, frame + offset, (size_t)length + 1 - offset, flags);
        if (sent < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (sent <= 0) return -1;
        offset += (size_t)sent;
    }
    for (size_t used = 0; used < sizeof(reply); ) {
        long long remaining = deadline - powerlock_milliseconds();
        if (remaining <= 0) { errno = ETIMEDOUT; return -1; }
        struct pollfd descriptor = {.fd = fd, .events = POLLIN};
        int ready = poll(&descriptor, 1, (int)remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { if (!ready) errno = ETIMEDOUT; return -1; }
        ssize_t count = recv(fd, reply + used, 1, MSG_DONTWAIT);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count <= 0) { if (!count) errno = ECONNRESET; return -1; }
        if (reply[used++] == '\0') {
            if (!strcmp(reply, "ok")) return 0;
            errno = EPROTO; return -1;
        }
    }
    errno = EPROTO;
    return -1;
}
#endif
