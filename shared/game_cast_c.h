#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void c1_game_cast_audio(const int16_t *data, size_t frames, unsigned channels, unsigned rate);
int c1_game_cast_local_audio(void);
#ifdef __cplusplus
}
#endif
