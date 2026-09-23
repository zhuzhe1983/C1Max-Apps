#pragma once
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <ctype.h>
#include <sys/types.h>
#include <assert.h>
unsigned get_ticks();
void wait_ticks(unsigned);
void pad_update();
unsigned short pad_read(int);
void video_flip();
void video_clear();
void port_printf(int,int,const char*);
extern unsigned short *SCREEN;
extern int c1_psx_mute;
