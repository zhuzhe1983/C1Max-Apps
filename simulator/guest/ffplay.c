#define _POSIX_C_SOURCE 200809L
/* ffplay CLI subset used by the unmodified C1auncher music-player.
 * Decoding and resampling execute in the MIPS guest, not on the host. */
#include "pcm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
#include <libswresample/swresample.h>

#define RATE 48000
#define CHANNELS 2
static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }

struct playback {
    int fd;
    SwrContext *swr;
    AVChannelLayout layout;
    enum AVSampleFormat format;
    int input_rate;
    uint8_t *buffer;
    unsigned int capacity;
    int64_t samples;
    double last_progress;
    double gain;
};

static int interrupted(void *unused)
{
    (void)unused;
    return stopped != 0;
}

static void progress(struct playback *play, int final)
{
    int queued = 0;
    if (ioctl(play->fd, SNDCTL_DSP_GETODELAY, &queued) < 0) queued = 0;
    double seconds = (play->samples - queued / (CHANNELS * 2)) / (double)RATE;
    if (seconds < 0) seconds = 0;
    if (final || seconds - play->last_progress >= 0.1) {
        fprintf(stderr, " %7.2f M-A: 0.000\r", seconds);
        play->last_progress = seconds;
    }
}

static int convert(struct playback *play, AVFrame *frame)
{
    if (stopped) return AVERROR_EXIT;
    if (frame && !play->swr) {
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        int result = av_channel_layout_copy(&play->layout, &frame->ch_layout);
        if (result < 0) return result;
        play->format = frame->format;
        play->input_rate = frame->sample_rate;
        result = swr_alloc_set_opts2(&play->swr, &stereo, AV_SAMPLE_FMT_S16, RATE,
                                    &frame->ch_layout, frame->format, frame->sample_rate, 0, NULL);
        if (result < 0) return result;
        result = swr_init(play->swr);
        if (result < 0) return result;
    }
    if (!play->swr) return 0;
    if (frame && (frame->sample_rate != play->input_rate || frame->format != play->format ||
                  av_channel_layout_compare(&frame->ch_layout, &play->layout)))
        return AVERROR_INVALIDDATA;
    int count = swr_get_out_samples(play->swr, frame ? frame->nb_samples : 0);
    if (count < 0) return count;
    if (count > 1024 * 1024) return AVERROR_INVALIDDATA;
    if (!count) return 0;
    av_fast_malloc(&play->buffer, &play->capacity, (size_t)count * CHANNELS * 2);
    if (!play->buffer) return AVERROR(ENOMEM);
    int converted = swr_convert(play->swr, &play->buffer, count,
                                frame ? (const uint8_t **)frame->extended_data : NULL,
                                frame ? frame->nb_samples : 0);
    if (converted < 0) return converted;
    if (play->gain != 1.0) {
        int16_t *samples = (int16_t *)play->buffer;
        for (int i = 0; i < converted * CHANNELS; ++i) {
            double value = samples[i] * play->gain;
            samples[i] = value > 32767 ? 32767 : value < -32768 ? -32768 : (int16_t)lrint(value);
        }
    }
    if (converted && pcm_write(play->fd, play->buffer, (size_t)converted * CHANNELS * 2, &stopped) < 0)
        return stopped ? AVERROR_EXIT : AVERROR(errno);
    play->samples += converted;
    progress(play, 0);
    return converted;
}

static int receive(AVCodecContext *codec, AVFrame *frame, struct playback *play)
{
    while (!stopped) {
        int result = avcodec_receive_frame(codec, frame);
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return 0;
        if (result < 0) return result;
        result = convert(play, frame);
        av_frame_unref(frame);
        if (result < 0) return result;
    }
    return AVERROR_EXIT;
}

static int decode(const char *path, const char *headers, double gain)
{
    AVFormatContext *input = avformat_alloc_context();
    AVDictionary *options = NULL;
    AVCodecContext *codec = NULL;
    AVPacket *packet = NULL;
    AVFrame *frame = NULL;
    struct playback play = { .fd = -1, .gain = gain };
    int result = AVERROR(ENOMEM);
    if (!input) goto done;
    input->interrupt_callback = (AVIOInterruptCB){ .callback = interrupted };
    if (headers && (result = av_dict_set(&options, "headers", headers, 0)) < 0) goto done;
    if ((result = av_dict_set(&options, "rw_timeout", "15000000", 0)) < 0 ||
        (result = av_dict_set(&options, "tls_verify", "1", 0)) < 0 ||
        (result = av_dict_set(&options, "ca_file", "/etc/ssl/certs/ca-certificates.crt", 0)) < 0) goto done;
    result = avformat_open_input(&input, path, NULL, &options);
    av_dict_free(&options);
    if (result < 0) goto done;
    result = avformat_find_stream_info(input, NULL);
    if (result < 0) goto done;
    const AVCodec *decoder = NULL;
    int stream = av_find_best_stream(input, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (stream < 0) { result = stream; goto done; }
    codec = avcodec_alloc_context3(decoder);
    if (!codec) { result = AVERROR(ENOMEM); goto done; }
    result = avcodec_parameters_to_context(codec, input->streams[stream]->codecpar);
    if (result < 0) goto done;
    codec->thread_count = 1;
    result = avcodec_open2(codec, decoder, NULL);
    if (result < 0) goto done;
    packet = av_packet_alloc(); frame = av_frame_alloc();
    if (!packet || !frame) { result = AVERROR(ENOMEM); goto done; }
    play.fd = pcm_open(RATE, CHANNELS);
    if (play.fd < 0) { result = AVERROR(errno); goto done; }
    if (input->duration != AV_NOPTS_VALUE && input->duration >= 0) {
        double duration = input->duration / (double)AV_TIME_BASE;
        int hours = (int)(duration / 3600), minutes = (int)(duration / 60) % 60;
        fprintf(stderr, "Duration: %02d:%02d:%05.2f\n", hours, minutes, duration - hours * 3600 - minutes * 60);
    }
    while (!stopped && (result = av_read_frame(input, packet)) >= 0) {
        if (packet->stream_index == stream) {
            result = avcodec_send_packet(codec, packet);
            if (result >= 0) result = receive(codec, frame, &play);
        }
        av_packet_unref(packet);
        if (result < 0) goto done;
    }
    if (stopped) { result = AVERROR_EXIT; goto done; }
    if (result != AVERROR_EOF) goto done;
    result = avcodec_send_packet(codec, NULL);
    if (result < 0 && result != AVERROR_EOF) goto done;
    result = receive(codec, frame, &play);
    if (result < 0) goto done;
    do { result = convert(&play, NULL); } while (result > 0 && !stopped);
    if (result < 0) goto done;
    if (!play.samples) { result = AVERROR_INVALIDDATA; goto done; }
    if (ioctl(play.fd, SNDCTL_DSP_SYNC, 0) < 0) { result = AVERROR(errno); goto done; }
    progress(&play, 1);
    result = 0;
done:
    av_dict_free(&options);
    if (play.fd >= 0) pcm_finish(play.fd, stopped || result < 0);
    swr_free(&play.swr); av_channel_layout_uninit(&play.layout);
    av_free(play.buffer);
    av_frame_free(&frame); av_packet_free(&packet);
    avcodec_free_context(&codec); avformat_close_input(&input);
    if (result < 0 && !stopped) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(result, message, sizeof(message));
        fprintf(stderr, "\nc1sim ffplay: %s\n", message);
    } else fprintf(stderr, "\n");
    return result < 0 && !stopped;
}

int main(int argc, char **argv)
{
    if (argc == 2 && (!strcmp(argv[1], "-version") || !strcmp(argv[1], "--version"))) {
        printf("c1sim ffplay compatibility player; FFmpeg %s; MIPS OSS AC97\n", av_version_info());
        return 0;
    }
    const char *path = NULL, *headers = NULL;
    double gain = 1.0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--") && i + 2 == argc) { path = argv[i + 1]; break; }
        if (!strcmp(argv[i], "-headers") && i + 1 < argc) { headers = argv[++i]; continue; }
        if (!strcmp(argv[i], "-af") && i + 1 < argc) {
            const char *filter = argv[++i];
            char *end = NULL;
            errno = 0;
            if (!strncmp(filter, "volume=", 7)) gain = strtod(filter + 7, &end);
            if (!end || end == filter + 7 || *end || errno || !isfinite(gain) || gain < 0) {
                fprintf(stderr, "c1sim ffplay: expected -af volume=NUMBER (finite and nonnegative)\n"); return 1;
            }
            continue;
        }
        if (strcmp(argv[i], "-nodisp") && strcmp(argv[i], "-vn") && strcmp(argv[i], "-autoexit") &&
            strcmp(argv[i], "-hide_banner") && strcmp(argv[i], "-stats")) {
            fprintf(stderr, "c1sim ffplay: unsupported argument %s\n", argv[i]); return 1;
        }
    }
    if (!path) { fprintf(stderr, "c1sim ffplay: expected -- FILE_OR_URL (MP3, FLAC or AAC)\n"); return 1; }
    struct sigaction action = { .sa_handler = stop };
    sigemptyset(&action.sa_mask);
    sigaction(SIGTERM, &action, NULL); sigaction(SIGINT, &action, NULL);
    av_log_set_level(AV_LOG_ERROR);
    avformat_network_init();
    int result = decode(path, headers, gain);
    avformat_network_deinit();
    return result;
}
