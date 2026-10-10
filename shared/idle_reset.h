#pragma once
#include <spawn.h>
#include <sys/wait.h>
#include <errno.h>
extern char **environ;

/* The stock PowerManager consumes this one-shot flag and clears its elapsed
 * backlight counter. Preserve the user's timer and all global lock settings.
 * Call only on activity/session boundaries, or while an app has active work.
 */
static inline int c1_reset_idle_flag(const char *name) {
    pid_t pid;
    char *argv[]={(char *)"setprop",(char *)name,(char *)"1",NULL};
    if(posix_spawn(&pid,"/usr/bin/setprop",NULL,NULL,argv,environ))return -1;
    int status;while(waitpid(pid,&status,0)<0){if(errno!=EINTR)return -1;}
    return WIFEXITED(status)&&WEXITSTATUS(status)==0?0:-1;
}
static inline int c1_reset_idle(void) {
    /* Once the stock manager enters its screen-off state it no longer polls
     * the backlight flag. Activity must cancel that second countdown too. */
    int suspend=c1_reset_idle_flag("sys.suspend.timer.reset");
    int backlight=c1_reset_idle_flag("sys.backlight.timer.reset");
    return suspend||backlight?-1:0;
}
