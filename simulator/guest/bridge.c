#define _GNU_SOURCE
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/pidfd.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FRAME_BYTES (340 * 800 * 4)
static int connection = -1, control = -1, framebuffer = -1, logs = -1;
static pid_t child = -1;
static char app[64] = "none";
static unsigned char previous[FRAME_BYTES], capture[FRAME_BYTES];
static unsigned sequence;
struct control_packet { uint16_t source, code; int32_t value; };
static void send_all(const void *data, size_t length)
{
    const char *bytes = data;
    while (length) {
        ssize_t count = send(connection, bytes, length, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) exit(1);
        bytes += count; length -= count;
    }
}
static void text(const char *message) { send_all(message, strlen(message)); }
static void stop_app(void)
{
    if (child > 0) kill(-child, SIGTERM);
    for (int pass = 0; pass < 5; ++pass) {
        if (child > 0 && pass == 4) kill(-child, SIGKILL);
        usleep(100000);
        while (waitpid(-1, NULL, WNOHANG) > 0) { }
    }
    /* Detached audio workers are adopted by the bridge. Stop them when the
       user explicitly switches via the web app selector or shuts down. */
    DIR *directory = opendir("/proc"); struct dirent *entry;
    while (directory && (entry = readdir(directory))) {
        int pid = atoi(entry->d_name), parent = 0; char path[96], line[256];
        if (pid <= 1 || pid == getpid()) continue;
        snprintf(path, sizeof(path), "/proc/%d/status", pid);
        FILE *file = fopen(path, "r"); if (!file) continue;
        while (fgets(line, sizeof(line), file)) if (sscanf(line, "PPid: %d", &parent) == 1) break;
        fclose(file);
        if (parent == getpid()) kill(pid, SIGKILL);
    }
    if (directory) closedir(directory);
    while (waitpid(-1, NULL, WNOHANG) > 0) { }
    child = -1;
    if (logs >= 0) close(logs);
    logs = -1;
}
static int valid_id(const char *id)
{
    if (!*id || strlen(id) > 32) return 0;
    for (const char *p = id; *p; ++p) if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-')) return 0;
    return 1;
}
static int app_path(const char *id, char *directory, size_t capacity)
{
    const char *roots[] = { "/storage/apps/data/appstore/current", "/storage/apps/current" };
    char binary[512];
    for (unsigned i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        snprintf(directory, capacity, "%s/%s", roots[i], id);
        snprintf(binary, sizeof(binary), "%s/c1max-%s", directory, !strcmp(id, "nes") ? "nes-browser" : id);
        if (!access(binary, X_OK)) return 1;
    }
    return 0;
}
static void inventory(void)
{
    FILE *file = fopen("/etc/c1max-apps", "r");
    char id[64], path[256], message[100];
    if (!file) return;
    while (fscanf(file, "%63s", id) == 1) {
        if (!valid_id(id)) continue;
        snprintf(message, sizeof(message), "INSTALLED %s %d\n", id, app_path(id, path, sizeof(path)));
        text(message);
    }
    fclose(file);
}
static void start_app(const char *id)
{
    char binary[512], directory[256], message[160]; int descriptors[2];
    if (!valid_id(id)) { text("ERROR invalid-app\n"); return; }
    if (!app_path(id, directory, sizeof(directory))) { text("ERROR app-not-installed\n"); return; }
    snprintf(binary, sizeof(binary), "%s/c1max-%s", directory, !strcmp(id, "nes") ? "nes-browser" : id);
    stop_app();
    if (pipe2(descriptors, O_CLOEXEC)) return;
    child = fork();
    if (!child) {
        setsid(); dup2(descriptors[1], 1); dup2(descriptors[1], 2);
        close(descriptors[0]); close(descriptors[1]); close(connection); close(control); close(framebuffer);
        int null = open("/dev/null", O_RDONLY); if (null >= 0) { dup2(null, 0); close(null); }
        chdir(directory);
        char *separator = strrchr(directory, '/');
        if (separator) { *separator = 0; setenv("C1_APPS_ROOT", directory, 1); }
        execl(binary, binary, !strcmp(id, "nes") ? "--roms" : NULL, NULL); perror("exec MIPS app"); _exit(127);
    }
    close(descriptors[1]);
    if (child < 0) { close(descriptors[0]); return; }
    logs = descriptors[0]; fcntl(logs, F_SETFL, O_NONBLOCK);
    snprintf(app, sizeof(app), "%s", id);
    snprintf(message, sizeof(message), "APP %s %d\n", app, child); text(message);
}
static void event(unsigned source, unsigned code, int value)
{
    struct control_packet packet = {source, code, value};
    if (write(control, &packet, sizeof(packet)) != sizeof(packet)) perror("input");
}
static void command(const char *line)
{
    char id[64]; unsigned source, code; int value, x, y, down;
    if (sscanf(line, "KEY %u %u %d", &source, &code, &value) == 3 && source < 2 && code <= KEY_MAX && value >= 0 && value <= 2)
        event(source, code, value);
    else if (sscanf(line, "TOUCH %d %d %d", &x, &y, &down) == 3 && x >= 0 && x < 340 && y >= 0 && y < 800 && (down == 0 || down == 1)) {
        event(2, ABS_X, x); event(2, ABS_Y, y); event(2, BTN_TOUCH, down); event(2, 0xffff, 0);
    } else if (sscanf(line, "BATTERY %d %u", &value, &code) == 2 && value >= 0 && value <= 100 && code <= 1) event(3, code, value);
    else if (sscanf(line, "RUN %63s", id) == 1) start_app(id);
    else if (!strcmp(line, "INVENTORY")) inventory();
    else if (!strcmp(line, "SHUTDOWN")) {
        stop_app();
        system("/bin/busybox killall c1max-volume >/dev/null 2>&1");
        usleep(200000); sync();
        if (umount("/storage")) perror("unmount C1 Max data");
        sync(); text("BYE\n");
    }
}
int main(int argc, char **argv)
{
    int port = argc > 1 ? atoi(argv[1]) : 7655;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(port)};
    char pending[4096]; size_t used = 0;
    if (port < 1 || port > 65535) return 2;
    signal(SIGPIPE, SIG_IGN); prctl(PR_SET_CHILD_SUBREAPER, 1);
    control = open("/dev/c1maxsim", O_WRONLY | O_CLOEXEC);
    framebuffer = open("/dev/fb2", O_RDWR | O_CLOEXEC);
    struct fb_fix_screeninfo fixed;
    if (control < 0 || framebuffer < 0 || ioctl(framebuffer, FBIOGET_FSCREENINFO, &fixed)) { perror("C1 Max driver"); return 1; }
    unsigned char *memory = mmap(NULL, fixed.smem_len, PROT_READ, MAP_SHARED, framebuffer, 0);
    if (memory == MAP_FAILED) return 1;
    inet_pton(AF_INET, "10.0.2.2", &address.sin_addr);
    for (;;) {
        connection = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (!connect(connection, (struct sockaddr *)&address, sizeof(address))) break;
        close(connection); usleep(100000);
    }
    text("READY c1max-mipsel\n"); inventory(); start_app("launcher");
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    time_t inventory_time = now.tv_sec + 5;
    for (;;) {
        struct pollfd waiters[] = {{connection, POLLIN, 0}, {logs, POLLIN, 0}};
        if (poll(waiters, 2, 16) < 0 && errno != EINTR) break;
        if (waiters[0].revents & (POLLHUP | POLLERR | POLLNVAL)) break;
        if (waiters[0].revents & POLLIN) {
            ssize_t count = recv(connection, pending + used, sizeof(pending) - used - 1, 0);
            if (count <= 0) break;
            used += count; pending[used] = 0; char *begin = pending, *end;
            while ((end = strchr(begin, '\n'))) { *end = 0; command(begin); begin = end + 1; }
            used -= begin - pending; memmove(pending, begin, used);
            if (used == sizeof(pending) - 1) break;
        }
        if (logs >= 0 && waiters[1].revents & (POLLIN | POLLHUP)) {
            char buffer[2048], header[80]; ssize_t size = read(logs, buffer, sizeof(buffer));
            if (size > 0) { snprintf(header, sizeof(header), "LOG %zd\n", size); text(header); send_all(buffer, size); }
            else if (!size) { close(logs); logs = -1; }
        }
        struct fb_var_screeninfo view;
        if (!ioctl(framebuffer, FBIOGET_VSCREENINFO, &view) && (uint64_t)view.yoffset * fixed.line_length + FRAME_BYTES <= fixed.smem_len) {
            memcpy(capture, memory + (size_t)view.yoffset * fixed.line_length, FRAME_BYTES);
            if (!sequence || memcmp(capture, previous, FRAME_BYTES)) {
                char header[80]; memcpy(previous, capture, FRAME_BYTES);
                snprintf(header, sizeof(header), "FRAME %u %d\n", ++sequence, FRAME_BYTES); text(header); send_all(capture, FRAME_BYTES);
            }
        }
        if (child > 0) {
            int status; pid_t ended = waitpid(child, &status, WNOHANG);
            if (ended == child) { char message[96]; snprintf(message, sizeof(message), "EXIT %d\n", WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status)); text(message); child = -1; }
        }
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec >= inventory_time) { inventory(); inventory_time = now.tv_sec + 5; }
    }
    stop_app(); return 0;
}
