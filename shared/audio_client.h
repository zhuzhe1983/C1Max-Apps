#ifndef C1_AUDIO_CLIENT_H
#define C1_AUDIO_CLIENT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define C1_AUDIO_MAGIC 0x43314131u
#define C1_AUDIO_QUEUE_MAX 128
enum c1_audio_owner { C1_AUDIO_NONE, C1_AUDIO_PIANO, C1_AUDIO_AIRTUNE, C1_AUDIO_STREAMPLAYER };
enum c1_audio_action { C1_AUDIO_STATUS, C1_AUDIO_PLAY, C1_AUDIO_APPEND, C1_AUDIO_PAUSE,
    C1_AUDIO_STOP, C1_AUDIO_SEEK, C1_AUDIO_NEXT, C1_AUDIO_PREVIOUS,
    C1_AUDIO_BACKGROUND, C1_AUDIO_DETACH, C1_AUDIO_SHUTDOWN };
enum c1_audio_kind { C1_AUDIO_URL, C1_AUDIO_SONG };
enum c1_audio_state { C1_AUDIO_IDLE, C1_AUDIO_CONNECTING, C1_AUDIO_PLAYING, C1_AUDIO_PAUSED, C1_AUDIO_ERROR };
struct c1_audio_request {
    uint32_t magic;
    int32_t action, owner, kind, index, value, background;
    int64_t duration_ms;
    char url[2048], title[256];
};
struct c1_audio_status {
    uint32_t magic;
    int32_t result, owner, state, background, index, count, song;
    int64_t position_ms, duration_ms;
    char url[2048], title[256], error[192];
};
/* Small bounded IPC calls; start only for a user-requested playback action. */
int c1_audio_call(struct c1_audio_request *,struct c1_audio_status *,int start);
int c1_audio_get(struct c1_audio_status *);
int c1_audio_command(int owner,int action,int value,struct c1_audio_status *);
/* value=-1 reads, 0/1 atomically persists the per-application preference. */
int c1_audio_background_preference(int owner,int value);
int c1_audio_socket_path(char *out,unsigned size);
void c1_audio_text(char *out,unsigned size,const char *input);
#ifdef __cplusplus
}
#endif
#endif
