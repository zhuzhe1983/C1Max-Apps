#define _GNU_SOURCE
/* MIPS H.264/AAC player: original apps consume the original Y4M FIFO protocol. */
#include "pcm.h"
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>

static volatile sig_atomic_t stopped;
static void stop(int signal) { (void)signal; stopped = 1; }
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
struct player {
    AVFormatContext *input;
    AVCodecContext *video, *audio;
    SwrContext *resample;
    AVPacket *packet;
    AVFrame *frame;
    int video_stream, audio_stream, fifo, pcm, silent, paused, seek_frame;
    int width, height, input_rate;
    enum AVSampleFormat input_format;
    AVChannelLayout input_layout;
    uint8_t *video_bytes, *audio_bytes;
    unsigned audio_capacity;
    double position, duration, epoch, start_time, seek_target;
    uint64_t frames;
    char commands[4096]; size_t command_size;
};
static int interrupted(void *unused) { (void)unused; return stopped; }

static int controls(struct player *p)
{
    struct pollfd input = {STDIN_FILENO, POLLIN, 0};
    if (poll(&input, 1, 0) <= 0 || !(input.revents & POLLIN)) return 0;
    ssize_t size = read(0, p->commands + p->command_size, sizeof(p->commands) - p->command_size - 1);
    if (size <= 0) return 0;
    p->command_size += size; p->commands[p->command_size] = 0;
    char *begin = p->commands, *end;
    while ((end = strchr(begin, '\n'))) {
        *end = 0; char *command = begin;
        if (!strncmp(command, "pausing_keep_force ", 19)) command += 19;
        else if (!strncmp(command, "pausing_keep ", 13)) command += 13;
        if (!strcmp(command, "quit")) stopped = 1;
        else if (!strcmp(command, "get_time_pos")) printf("ANS_TIME_POSITION=%.3f\n", p->position);
        else if (!strcmp(command, "pause")) {
            p->paused = !p->paused;
            if (p->pcm >= 0) ioctl(p->pcm, SNDCTL_DSP_RESET, 0);
            if (!p->paused) p->epoch = now() - p->position;
        } else {
            double value; int mode;
            if (sscanf(command, "seek %lf %d", &value, &mode) == 2 && isfinite(value) && (mode == 0 || mode == 2))
                p->seek_target = fmax(0, fmin(p->duration > 0 ? p->duration : 86400, mode == 0 ? p->position + value : value));
        }
        begin = end + 1;
    }
    p->command_size -= begin - p->commands; memmove(p->commands, begin, p->command_size);
    if (p->command_size == sizeof(p->commands) - 1) return AVERROR_INVALIDDATA;
    return 0;
}
static int send_all(int fd, const uint8_t *bytes, size_t size)
{
    while (size && !stopped) {
        ssize_t sent = write(fd, bytes, size);
        if (sent > 0) { bytes += sent; size -= sent; continue; }
        if (sent < 0 && errno == EINTR) continue;
        if (sent < 0 && errno == EAGAIN) { struct pollfd output = {fd, POLLOUT, 0}; poll(&output, 1, 20); continue; }
        return AVERROR(errno ? errno : EIO);
    }
    return stopped ? AVERROR_EXIT : 0;
}
static int audio_frame(struct player *p, AVFrame *frame)
{
    if (p->silent || p->paused || p->seek_frame) return 0;
    if (!p->resample || frame->sample_rate != p->input_rate || frame->format != p->input_format ||
        av_channel_layout_compare(&frame->ch_layout, &p->input_layout)) {
        swr_free(&p->resample); av_channel_layout_uninit(&p->input_layout);
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        int result = swr_alloc_set_opts2(&p->resample, &stereo, AV_SAMPLE_FMT_S16, 48000,
            &frame->ch_layout, frame->format, frame->sample_rate, 0, NULL);
        if (result < 0 || (result = swr_init(p->resample)) < 0) return result;
        if ((result = av_channel_layout_copy(&p->input_layout, &frame->ch_layout)) < 0) return result;
        p->input_rate = frame->sample_rate; p->input_format = frame->format;
    }
    int count = swr_get_out_samples(p->resample, frame->nb_samples);
    if (count < 0 || count > 1024 * 1024) return AVERROR_INVALIDDATA;
    av_fast_malloc(&p->audio_bytes, &p->audio_capacity, (size_t)count * 4);
    if (!p->audio_bytes) return AVERROR(ENOMEM);
    count = swr_convert(p->resample, &p->audio_bytes, count, (const uint8_t **)frame->extended_data, frame->nb_samples);
    if (count < 0) return count;
    return count && pcm_write(p->pcm, p->audio_bytes, (size_t)count * 4, &stopped) < 0 ? AVERROR(errno) : 0;
}
static int video_frame(struct player *p, AVFrame *frame)
{
    if (frame->format != AV_PIX_FMT_YUV420P || (frame->flags & AV_FRAME_FLAG_INTERLACED) || frame->width < 2 || frame->height < 2 ||
        frame->width > 960 || frame->height > 960 || frame->width * frame->height > 307200 ||
        (frame->width & 1) || (frame->height & 1)) { puts("C1_YUV_ERROR=unsupported_geometry_or_format"); return AVERROR_INVALIDDATA; }
    int64_t timestamp = frame->best_effort_timestamp;
    double position = timestamp == AV_NOPTS_VALUE ? p->position : timestamp * av_q2d(p->input->streams[p->video_stream]->time_base) - p->start_time;
    if (position < 0) position = 0;
    if (p->seek_frame && position + .001 < p->position) return 0;
    if (!p->epoch) p->epoch = now() - position;
    while (!stopped && !p->paused && now() < p->epoch + position) {
        int result = controls(p); if (result < 0) return result;
        if (p->seek_target >= 0) return 0;
        usleep(5000);
    }
    if (stopped) return AVERROR_EXIT;
    while (p->paused && !p->seek_frame && !stopped && p->seek_target < 0) { controls(p); usleep(10000); }
    if (stopped) return AVERROR_EXIT;
    if (p->seek_target >= 0) return 0;
    int width = frame->width, height = frame->height;
    if (width > 400) { height = (height * 400 / width) & ~1; width = 400; }
    if (height > 288) { width = (width * 288 / height) & ~1; height = 288; }
    if (width < 2) width = 2;
    if (height < 2) height = 2;
    if (width != p->width || height != p->height) {
        p->width = width; p->height = height; av_freep(&p->video_bytes);
        p->video_bytes = av_malloc((size_t)width * height * 3 / 2 + 128);
        if (!p->video_bytes) return AVERROR(ENOMEM);
        AVRational aspect = frame->sample_aspect_ratio;
        if (aspect.num < 1 || aspect.den < 1) aspect = (AVRational){1, 1};
        char header[160]; int size = snprintf(header, sizeof(header), "YUV4MPEG2 W%d H%d F20:1 Ip A%d:%d C420jpeg\n", width, height, aspect.num, aspect.den);
        int result = send_all(p->fifo, (const uint8_t *)header, size); if (result < 0) return result;
    }
    size_t used = snprintf((char *)p->video_bytes, 128, "FRAME Xpts=%lld\n", (long long)llrint(position * 90000));
    for (int plane = 0; plane < 3; ++plane) {
        int w = plane ? width / 2 : width, h = plane ? height / 2 : height;
        int sw = plane ? frame->width / 2 : frame->width, sh = plane ? frame->height / 2 : frame->height;
        if (!frame->data[plane] || abs(frame->linesize[plane]) < sw) return AVERROR_INVALIDDATA;
        for (int y = 0; y < h; ++y) {
            const uint8_t *source = frame->data[plane] + (ptrdiff_t)(y * sh / h) * frame->linesize[plane];
            for (int x = 0; x < w; ++x) p->video_bytes[used++] = source[x * sw / w];
        }
    }
    int result = send_all(p->fifo, p->video_bytes, used); if (result < 0) return result;
    p->position = position; p->seek_frame = 0; ++p->frames;
    if (p->frames == 1) puts("Starting playback...");
    return 0;
}
static int receive(struct player *p, AVCodecContext *codec, int video)
{
    while (!stopped) {
        int result = avcodec_receive_frame(codec, p->frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return 0;
        if (result < 0) return result;
        result = video ? video_frame(p, p->frame) : audio_frame(p, p->frame);
        av_frame_unref(p->frame); if (result < 0) return result;
        if (video && p->paused && !p->seek_frame) return 0;
    }
    return AVERROR_EXIT;
}
static int codec(struct player *p, enum AVMediaType type, int *stream, AVCodecContext **output)
{
    const AVCodec *decoder;
    *stream = av_find_best_stream(p->input, type, -1, -1, &decoder, 0);
    if (*stream < 0) return type == AVMEDIA_TYPE_AUDIO ? 0 : *stream;
    *output = avcodec_alloc_context3(decoder); if (!*output) return AVERROR(ENOMEM);
    int result = avcodec_parameters_to_context(*output, p->input->streams[*stream]->codecpar);
    (*output)->thread_count = 1;
    return result < 0 ? result : avcodec_open2(*output, decoder, NULL);
}
static int seek(struct player *p)
{
    double target = p->seek_target; p->seek_target = -1;
    int64_t timestamp = (int64_t)((target + p->start_time) * AV_TIME_BASE);
    int result = avformat_seek_file(p->input, -1, INT64_MIN, timestamp, timestamp, AVSEEK_FLAG_BACKWARD);
    if (result < 0) { puts("ANS_ERROR=seek_failed"); return 0; }
    avcodec_flush_buffers(p->video); if (p->audio) avcodec_flush_buffers(p->audio);
    swr_free(&p->resample); av_channel_layout_uninit(&p->input_layout);
    if (p->pcm >= 0) ioctl(p->pcm, SNDCTL_DSP_RESET, 0);
    p->position = target; p->seek_frame = 1; p->epoch = now() - target;
    return 0;
}
int main(int argc, char **argv)
{
    const char *path = NULL, *headers = "", *fifo = getenv("C1_YUV_FIFO");
    struct player p = {.fifo = -1, .pcm = -1, .audio_stream = -1, .seek_target = -1};
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--headers") && i + 1 < argc) headers = argv[++i];
        else if (!strcmp(argv[i], "--silent") && i + 1 < argc) p.silent = !strcmp(argv[++i], "1");
        else if (!strcmp(argv[i], "--") && i + 1 == argc - 1) { path = argv[++i]; break; }
        else return 2;
    }
    if (!path || !fifo || fifo[0] != '/') return 2;
    signal(SIGPIPE, SIG_IGN); signal(SIGTERM, stop); signal(SIGINT, stop); setvbuf(stdout, NULL, _IOLBF, 0);
    int flags = fcntl(0, F_GETFL); if (flags >= 0) fcntl(0, F_SETFL, flags | O_NONBLOCK);
    AVDictionary *options = NULL; int result = AVERROR(ENOMEM);
    p.input = avformat_alloc_context(); if (!p.input) goto done;
    p.input->interrupt_callback = (AVIOInterruptCB){.callback = interrupted};
    av_log_set_level(AV_LOG_ERROR);
    av_dict_set(&options, "headers", headers, 0); av_dict_set(&options, "rw_timeout", "15000000", 0);
    av_dict_set(&options, "tls_verify", "1", 0); av_dict_set(&options, "ca_file", "/etc/ssl/certs/ca-certificates.crt", 0);
    if ((result = avformat_open_input(&p.input, path, NULL, &options)) < 0) goto done;
    av_dict_free(&options);
    if ((result = avformat_find_stream_info(p.input, NULL)) < 0 ||
        (result = codec(&p, AVMEDIA_TYPE_VIDEO, &p.video_stream, &p.video)) < 0 ||
        (result = codec(&p, AVMEDIA_TYPE_AUDIO, &p.audio_stream, &p.audio)) < 0) goto done;
    p.duration = p.input->duration == AV_NOPTS_VALUE ? 0 : p.input->duration / (double)AV_TIME_BASE;
    p.start_time = p.input->start_time == AV_NOPTS_VALUE ? 0 : p.input->start_time / (double)AV_TIME_BASE;
    printf("ID_LENGTH=%.3f\n", p.duration);
    p.fifo = open(fifo, O_WRONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
    if (p.fifo < 0) { result = AVERROR(errno); goto done; }
    struct stat info;
    if (fstat(p.fifo, &info) || !S_ISFIFO(info.st_mode)) { result = AVERROR(EINVAL); goto done; }
    if (!p.silent && p.audio) { p.pcm = pcm_open(48000, 2); if (p.pcm < 0) { result = AVERROR(errno); goto done; } }
    p.packet = av_packet_alloc(); p.frame = av_frame_alloc();
    if (!p.packet || !p.frame) { result = AVERROR(ENOMEM); goto done; }
    while (!stopped) {
        if ((result = controls(&p)) < 0) break;
        if (p.seek_target >= 0) { if ((result = seek(&p)) < 0) break; }
        if (p.paused && !p.seek_frame) { usleep(10000); continue; }
        if ((result = av_read_frame(p.input, p.packet)) < 0) break;
        AVCodecContext *decoder = p.packet->stream_index == p.video_stream ? p.video : p.packet->stream_index == p.audio_stream ? p.audio : NULL;
        if (decoder) {
            result = avcodec_send_packet(decoder, p.packet);
            if (result == AVERROR(EAGAIN)) {
                result = receive(&p, decoder, decoder == p.video);
                if (result >= 0) result = avcodec_send_packet(decoder, p.packet);
            }
            if (result >= 0) result = receive(&p, decoder, decoder == p.video);
        }
        av_packet_unref(p.packet); if (result < 0) break;
    }
    if (result == AVERROR_EOF) {
        avcodec_send_packet(p.video, NULL); result = receive(&p, p.video, 1);
        if (p.audio) { avcodec_send_packet(p.audio, NULL); if (result >= 0) result = receive(&p, p.audio, 0); }
    }
done:
    if (result < 0 && result != AVERROR_EXIT) { char error[128]; av_strerror(result, error, sizeof(error)); fprintf(stderr, "c1max video: %s\n", error); }
    fprintf(stderr, "C1MAX_VIDEO frames=%llu position=%.3f\n", (unsigned long long)p.frames, p.position);
    if (p.pcm >= 0) pcm_finish(p.pcm, stopped || result < 0);
    if (p.fifo >= 0) close(p.fifo);
    av_free(p.video_bytes); av_free(p.audio_bytes); av_channel_layout_uninit(&p.input_layout); swr_free(&p.resample);
    av_frame_free(&p.frame); av_packet_free(&p.packet); avcodec_free_context(&p.video); avcodec_free_context(&p.audio);
    avformat_close_input(&p.input); av_dict_free(&options);
    return stopped || result >= 0 || result == AVERROR_EXIT ? 0 : 1;
}
