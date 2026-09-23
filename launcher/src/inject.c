/* inject.c - synthesize a touch gesture on /dev/input/event2 (cst3xx) for
 * testing the launcher without a physical finger. Feeds ABS_X/ABS_Y + BTN_TOUCH
 * in the panel-native frame (ABS_X = native col 0..339, ABS_Y = native row
 * 0..799), exactly what the driver emits.
 *
 * usage: inject <ax0> <ay0> <ax1> <ay1> <steps> <step_ms>
 *   a swipe from raw (ax0,ay0) to (ax1,ay1). A drag with steps>1 scrolls;
 *   steps==1 (same start/end) is a tap.
 */
#define _GNU_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <linux/input.h>

static int fd;
static void emit(int type, int code, int val){
    struct input_event e; memset(&e,0,sizeof e);
    e.type=type; e.code=code; e.value=val;
    if(write(fd,&e,sizeof e)!=sizeof e) perror("write");
}
static void syn(void){ emit(EV_SYN, SYN_REPORT, 0); }
static void msleep(int ms){ struct timespec t={ms/1000,(long)(ms%1000)*1000000L}; nanosleep(&t,0); }

int main(int argc, char**argv){
    int ax0,ay0,ax1,ay1,steps,ms,i;
    if(argc<7){ fprintf(stderr,"usage: %s ax0 ay0 ax1 ay1 steps step_ms\n",argv[0]); return 2; }
    ax0=atoi(argv[1]); ay0=atoi(argv[2]); ax1=atoi(argv[3]); ay1=atoi(argv[4]);
    steps=atoi(argv[5]); ms=atoi(argv[6]); if(steps<1) steps=1;
    fd=open("/dev/input/event2", O_WRONLY);
    if(fd<0){ perror("open event2"); return 1; }
    /* down */
    emit(EV_ABS, ABS_X, ax0); emit(EV_ABS, ABS_Y, ay0);
    emit(EV_KEY, BTN_TOUCH, 1); syn(); msleep(ms);
    for(i=1;i<=steps;i++){
        int x = ax0 + (ax1-ax0)*i/steps;
        int y = ay0 + (ay1-ay0)*i/steps;
        emit(EV_ABS, ABS_X, x); emit(EV_ABS, ABS_Y, y); syn();
        msleep(ms);
    }
    /* up */
    emit(EV_KEY, BTN_TOUCH, 0); syn();
    close(fd);
    return 0;
}
