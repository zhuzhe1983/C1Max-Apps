#pragma once
#include <stdint.h>
int typeface_open(const char *path);
void typeface_close(void);
int typeface_width(const char *text, int pixels);
void typeface_draw(uint32_t *out, int width, int height, int x, int y,
                   const char *text, uint32_t color, int pixels);
