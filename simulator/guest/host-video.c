#define _GNU_SOURCE
/* MIPS 加速客户端：接收本机解码帧，沿用原应用 FIFO 和客体 AC97 音频。 */
#include "pcm.h"
#include <arpa/inet.h>
#include <math.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>

static volatile sig_atomic_t stopped;
static void stop(int signal) { (void)signal; stopped = 1; }
struct video {
    int socket, fifo, pcm, width, height, rate_n, rate_d, aspect_n, aspect_d;
    int pending, normal_end, error;
    unsigned generation;
    size_t size;
    double position;
    uint8_t *pixels;
    uint64_t received, frames, audio_bytes;
    pthread_mutex_t lock;
    pthread_cond_t changed;
};
static int send_bytes(int fd, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    while (size && !stopped) {
        ssize_t count = send(fd, bytes, size, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        bytes += count; size -= count;
    }
    return stopped ? -1 : 0;
}
static int receive_bytes(int fd, void *data, size_t size)
{
    uint8_t *bytes = data;
    while (size && !stopped) {
        ssize_t count = recv(fd, bytes, size, 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return -1;
        bytes += count; size -= count;
    }
    return stopped ? -1 : 0;
}
static int line(int fd, char *buffer, size_t capacity)
{
    size_t used = 0;
    while (used + 1 < capacity) {
        if (receive_bytes(fd, buffer + used, 1)) return -1;
        if (buffer[used++] == '\n') { buffer[used] = 0; return 0; }
    }
    return -1;
}
static int fifo_write(int fd, const void *data, size_t size)
{
    const uint8_t *bytes = data;
    while (size && !stopped) {
        ssize_t count = write(fd, bytes, size);
        if (count > 0) { bytes += count; size -= count; continue; }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && errno == EAGAIN) { struct pollfd output = {fd, POLLOUT, 0}; poll(&output, 1, 20); continue; }
        return -1;
    }
    return stopped ? -1 : 0;
}
static void *frames(void *argument)
{
    struct video *v = argument; uint8_t *packet = NULL; int header_sent = 0;
    while (!stopped) {
        pthread_mutex_lock(&v->lock);
        while (!v->pending && !stopped) pthread_cond_wait(&v->changed, &v->lock);
        if (stopped) { pthread_mutex_unlock(&v->lock); break; }
        if (!packet) packet = malloc(v->size + 128);
        if (!packet) { v->error = 1; stopped = 1; pthread_mutex_unlock(&v->lock); break; }
        double position = v->position; memcpy(packet + 128, v->pixels, v->size); v->pending = 0;
        pthread_mutex_unlock(&v->lock);
        if (!header_sent) {
            char header[160]; int n = snprintf(header, sizeof(header), "YUV4MPEG2 W%d H%d F%d:%d Ip A%d:%d C420jpeg\n", v->width, v->height, v->rate_n, v->rate_d, v->aspect_n, v->aspect_d);
            if (fifo_write(v->fifo, header, n)) break;
            header_sent = 1;
        }
        char marker[128]; int n = snprintf(marker, sizeof(marker), "FRAME Xpts=%lld\n", (long long)llrint(position * 90000));
        if (fifo_write(v->fifo, marker, n) || fifo_write(v->fifo, packet + 128, v->size)) break;
        if (!v->frames++) puts("Starting playback...");
    }
    if (!stopped) { v->error = 1; stopped = 1; shutdown(v->socket, SHUT_RDWR); }
    free(packet); return NULL;
}
static void *receive(void *argument)
{
    struct video *v = argument; char message[512]; uint8_t *bytes = malloc(1024 * 1024);
    if (!bytes) { v->error = 1; stopped = 1; return NULL; }
    while (!stopped && !line(v->socket, message, sizeof(message))) {
        if (message[0] == 'M') {
            double duration; int audio;
            if (v->pixels || sscanf(message, "M %lf %d %d %d %d %d %d %d", &duration, &v->width, &v->height, &v->rate_n, &v->rate_d, &v->aspect_n, &v->aspect_d, &audio) != 8 ||
                !isfinite(duration) || duration <= 0 || duration > 86400 || v->width < 2 || v->width > 400 || v->height < 2 || v->height > 288 ||
                (v->width & 1) || (v->height & 1) || v->rate_n < 1 || v->rate_d < 1 || v->rate_n > 60 * v->rate_d ||
                v->aspect_n < 1 || v->aspect_d < 1 || v->aspect_n > 1000000 || v->aspect_d > 1000000 || (audio != 0 && audio != 1)) break;
            v->size = (size_t)v->width * v->height * 3 / 2; v->pixels = malloc(v->size);
            if (!v->pixels) break;
            if (audio && (v->pcm = pcm_open(48000, 2)) < 0) break;
            printf("ID_LENGTH=%.3f\n", duration);
        } else if (message[0] == 'V' || message[0] == 'A') {
            double position; unsigned size;
            if (sscanf(message + 1, "%lf %u", &position, &size) != 2 || !isfinite(position) || position < 0 || position > 86401 || !v->pixels ||
                size > 1024 * 1024 || (message[0] == 'V' ? size != v->size : size % 4 != 0) || receive_bytes(v->socket, bytes, size)) break;
            if (message[0] == 'V') {
                pthread_mutex_lock(&v->lock);
                memcpy(v->pixels, bytes, size); v->position = position; v->pending = 1; ++v->received;
                pthread_cond_signal(&v->changed); pthread_mutex_unlock(&v->lock);
            } else if (v->pcm >= 0) {
                if (pcm_write(v->pcm, bytes, size, &stopped)) break;
                v->audio_bytes += size;
            }
        } else if (message[0] == 'T') {
            double position;
            if (sscanf(message + 1, "%lf", &position) != 1 || !isfinite(position) || position < 0 || position > 86401) break;
            printf("ANS_TIME_POSITION=%.3f\n", position);
        } else if (message[0] == 'S') {
            unsigned generation; double position; int paused;
            if (sscanf(message, "S %u %lf %d", &generation, &position, &paused) != 3) break;
            pthread_mutex_lock(&v->lock); v->generation = generation; v->pending = 0; pthread_mutex_unlock(&v->lock);
            if (v->pcm >= 0) ioctl(v->pcm, SNDCTL_DSP_RESET, 0);
        } else if (message[0] == 'E') {
            v->normal_end = !strncmp(message, "E 0", 3); break;
        } else if (message[0] == 'X') {
            if (strstr(message, "http_403")) fputs("HTTP error 403 Forbidden\n", stderr);
            else if (strstr(message, "unsupported_format")) puts("C1_YUV_ERROR=unsupported_format");
            else fputs("c1max host video: decode failed\n", stderr);
            break;
        } else break;
    }
    if (!v->normal_end && !stopped) v->error = 1;
    stopped = 1;
    pthread_mutex_lock(&v->lock); pthread_cond_broadcast(&v->changed); pthread_mutex_unlock(&v->lock);
    free(bytes); return NULL;
}
int main(int argc, char **argv)
{
    const char *path = NULL, *headers = "", *silent = "0", *fifo = getenv("C1_YUV_FIFO");
    const char *port = getenv("C1SIM_HTTP"), *token = getenv("C1SIM_TOKEN");
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headers") && i + 1 < argc) headers = argv[++i];
        else if (!strcmp(argv[i], "--silent") && i + 1 < argc) silent = argv[++i];
        else if (!strcmp(argv[i], "--") && i + 1 == argc - 1) { path = argv[++i]; break; }
        else return 2;
    }
    if (!path || !fifo || !port || !token || strpbrk(path, "\r\n") || strpbrk(token, "\r\n") || (strcmp(silent, "0") && strcmp(silent, "1"))) return 2;
    struct video v = {.socket = -1, .fifo = -1, .pcm = -1, .lock = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER};
    signal(SIGPIPE, SIG_IGN); signal(SIGTERM, stop); signal(SIGINT, stop); setvbuf(stdout, NULL, _IOLBF, 0);
    int file = -1; off_t length = 0; struct stat info;
    if (strncmp(path, "http://", 7) && strncmp(path, "https://", 8)) {
        file = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        if (file < 0 || fstat(file, &info) || !S_ISREG(info.st_mode) || info.st_size < 1 || info.st_size > 64 * 1024 * 1024) goto failed;
        length = info.st_size;
    }
    v.fifo = open(fifo, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (v.fifo < 0 || fstat(v.fifo, &info) || !S_ISFIFO(info.st_mode)) goto failed;
    v.socket = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(atoi(port))}; inet_pton(AF_INET, "10.0.2.2", &address.sin_addr);
    struct timeval timeout = {30, 0}; int enabled = 1;
    setsockopt(v.socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); setsockopt(v.socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(v.socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
    if (connect(v.socket, (struct sockaddr *)&address, sizeof(address))) goto failed;
    char request[16384]; int size = snprintf(request, sizeof(request), "POST /video HTTP/1.1\r\nHost: 10.0.2.2\r\nConnection: Upgrade\r\nUpgrade: c1max-video-v1\r\nX-C1Sim-Token: %s\r\nX-C1Max-URL: %s\r\nX-C1Max-Silent: %s\r\nContent-Length: %lld\r\n%s\r\n", token, path, silent, (long long)length, headers);
    if (size < 0 || size >= (int)sizeof(request) || send_bytes(v.socket, request, size)) goto failed;
    uint8_t buffer[32768];
    while (length > 0 && !stopped) {
        ssize_t count = read(file, buffer, length < (off_t)sizeof(buffer) ? (size_t)length : sizeof(buffer));
        if (count <= 0 || send_bytes(v.socket, buffer, count)) goto failed;
        length -= count;
    }
    if (file >= 0) { close(file); file = -1; }
    char response[512]; int status;
    if (line(v.socket, response, sizeof(response)) || sscanf(response, "HTTP/%*s %d", &status) != 1 || status != 101) goto failed;
    do { if (line(v.socket, response, sizeof(response))) goto failed; } while (strcmp(response, "\r\n"));
    pthread_t receiver, writer;
    if (pthread_create(&writer, NULL, frames, &v)) goto failed;
    if (pthread_create(&receiver, NULL, receive, &v)) {
        stopped = 1; pthread_mutex_lock(&v.lock); pthread_cond_broadcast(&v.changed); pthread_mutex_unlock(&v.lock);
        pthread_join(writer, NULL); goto failed;
    }
    int input_fd = STDIN_FILENO;
    while (!stopped) {
        struct pollfd input = {input_fd, POLLIN, 0};
        if (poll(&input, 1, 100) > 0 && input.revents & POLLIN) {
            ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count > 0 && send_bytes(v.socket, buffer, count)) break;
            if (count == 0) input_fd = -1;
        }
    }
    stopped = 1; shutdown(v.socket, SHUT_RDWR);
    pthread_mutex_lock(&v.lock); pthread_cond_broadcast(&v.changed); pthread_mutex_unlock(&v.lock);
    pthread_join(receiver, NULL); pthread_join(writer, NULL);
    if (v.pcm >= 0) pcm_finish(v.pcm, !v.normal_end);
    fprintf(stderr, "C1MAX_HOST_VIDEO frames=%llu dropped=%llu audio_bytes=%llu\n", (unsigned long long)v.frames, (unsigned long long)(v.received - v.frames), (unsigned long long)v.audio_bytes);
    close(v.socket); close(v.fifo); free(v.pixels); return v.error;
failed:
    fputs("c1max host video: bridge unavailable\n", stderr);
    if (file >= 0) close(file);
    if (v.socket >= 0) close(v.socket);
    if (v.fifo >= 0) close(v.fifo);
    return 1;
}
