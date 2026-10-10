#ifndef C1_GAME_AWAKE_H
#define C1_GAME_AWAKE_H
#include <fcntl.h>
#include <sys/un.h>
#include "power_lock.h"
#ifndef C1_POWERLOCK_PATH
#define C1_POWERLOCK_PATH "/dev/socket/PowerLock"
#endif

/* Owned by the launcher's wait loop, never inherited by an app. The vendor
 * drops these per-client locks when the socket closes, including on a crash. */
typedef struct { int fd; long long next_check; int warned; } C1GameAwake;
static inline int c1_game_needs_awake(const char *id) {
    return id&&(!strcmp(id,"nes")||!strcmp(id,"pcsx4all")||!strcmp(id,"dosbox"));
}
static inline void c1_game_awake_end(C1GameAwake *g) {
    if(g->fd>=0){
        powerlock_command_timeout(g->fd,"blunlock",250);
        powerlock_command_timeout(g->fd,"susunlock",250);
        close(g->fd);g->fd=-1;
    }
}
static inline void c1_game_awake_tick(C1GameAwake *g,long long now) {
    if(now<g->next_check)return;
    g->next_check=now+5000;
    if(g->fd<0){
        g->fd=socket(AF_UNIX,SOCK_STREAM,0);
        if(g->fd>=0){
            if(fcntl(g->fd,F_SETFD,FD_CLOEXEC)<0||fcntl(g->fd,F_SETFL,O_NONBLOCK)<0){
                close(g->fd);g->fd=-1;
            }
        }
        if(g->fd>=0){
            struct sockaddr_un addr={0};addr.sun_family=AF_UNIX;
            snprintf(addr.sun_path,sizeof addr.sun_path,"%s",C1_POWERLOCK_PATH);
            if(connect(g->fd,(struct sockaddr *)&addr,sizeof addr)<0){close(g->fd);g->fd=-1;}
        }
    }
    if(g->fd>=0&&powerlock_command_timeout(g->fd,"suslock",250)==0&&
       powerlock_command_timeout(g->fd,"bllock",250)==0){
        if(g->warned)fprintf(stderr,"[launcher] Game awake locks restored\n");
        g->warned=0;return;
    }
    if(!g->warned)fprintf(stderr,"[launcher] Game awake locks unavailable; retrying\n");
    g->warned=1;
    /* No partial lock is left behind after a failed acknowledgement. */
    if(g->fd>=0){close(g->fd);g->fd=-1;}
}
#endif
