/* Exercise the latest upstream audio IPC and MIPS decoder with an owned HTTP fixture. */
#define _GNU_SOURCE
#include "audio_client.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    struct c1_audio_request request = {0};
    struct c1_audio_status status;
    /* owner=NONE starts the shared runtime; per-app owners select their own bundled daemon. */
    request.action = C1_AUDIO_STATUS;
    int ok = !c1_audio_call(&request, &status, 1);
    request.action = C1_AUDIO_PLAY; request.owner = C1_AUDIO_AIRTUNE;
    request.kind = C1_AUDIO_URL; request.duration_ms = 6000;
    snprintf(request.url, sizeof request.url, "%s", argv[1]);
    strcpy(request.title, "simulator audio regression");
    if (ok) ok = !c1_audio_call(&request, &status, 0);
    for (int i = 0; ok && i < 60; ++i) {
        usleep(100000);
        if (c1_audio_get(&status) || status.state == C1_AUDIO_ERROR) { ok = 0; break; }
        if (status.state == C1_AUDIO_PLAYING && status.position_ms >= 500) break;
    }
    ok = ok && status.state == C1_AUDIO_PLAYING && status.position_ms >= 500;
    long long position = ok ? (long long)status.position_ms : 0;
    if (ok) ok = !c1_audio_command(C1_AUDIO_AIRTUNE, C1_AUDIO_PAUSE, 0, &status) && status.state == C1_AUDIO_PAUSED;
    if (ok) {
        usleep(200000);
        ok = !c1_audio_get(&status) && status.state == C1_AUDIO_PAUSED;
    }
    if (ok) ok = !c1_audio_command(C1_AUDIO_AIRTUNE, C1_AUDIO_PAUSE, 0, &status) && status.state == C1_AUDIO_PLAYING;
    int stopped = !c1_audio_command(0, C1_AUDIO_SHUTDOWN, 0, &status);
    printf("{\"playing\":%s,\"position_ms\":%lld,\"pause_resume\":%s,\"shutdown\":%s}\n", ok ? "true" : "false", position, ok ? "true" : "false", stopped ? "true" : "false");
    return ok && stopped ? 0 : 1;
}
