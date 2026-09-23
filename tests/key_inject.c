/* Device-only QA: synthesize a key/chord on our foreground app.
 * c1max-key-test [--hold-ms 1800] 0|1 KEYCODE [KEYCODE...]
 */
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
static int fd;
static volatile sig_atomic_t stopping;
static void stop(int sig) { (void)sig; stopping = 1; }
static int number(const char *text, long maximum, int *out) {
    if (!text || !*text) return -1;
    for (const char *p=text; *p; ++p) if (*p<'0'||*p>'9') return -1;
    errno=0; char *end=NULL; long value=strtol(text,&end,10);
    if (errno || *end || value<0 || value>maximum) return -1;
    *out=(int)value; return 0;
}
static int key(int code,int value) {
    struct input_event e; memset(&e,0,sizeof e);
    e.type=EV_KEY; e.code=code; e.value=value;
    if(write(fd,&e,sizeof e)!=sizeof e) { perror("key"); return -1; }
    memset(&e,0,sizeof e); e.type=EV_SYN; e.code=SYN_REPORT;
    if(write(fd,&e,sizeof e)!=sizeof e) { perror("SYN_REPORT"); return -1; }
    usleep(80000); return 0;
}
int main(int argc,char**argv) {
    int arg=1,hold_ms=0,keys[6],count=0,pressed=0,result=0;
    if(argc>1&&!strcmp(argv[1],"--hold-ms")) {
        if(argc<5||number(argv[2],60000,&hold_ms)) return 2;
        arg=3;
    }
    if(argc-arg<2||argc-arg>7) return 2;
    if(strcmp(argv[arg],"0")&&strcmp(argv[arg],"1")) return 2;
    const char *device=argv[arg++][0]=='1'?"/dev/input/event1":"/dev/input/event0";
    for(;arg<argc;++arg) {
        if(number(argv[arg],KEY_MAX,&keys[count])||!keys[count]) return 2;
        ++count;
    }
    signal(SIGTERM,stop); signal(SIGINT,stop); signal(SIGHUP,stop);
    fd=open(device,O_WRONLY|O_CLOEXEC);
    if(fd<0) { perror(device); return 1; }
    for(int i=0;i<count&&!stopping;i++) {
        ++pressed;  /* Attempt release even if the following SYN write fails. */
        if(key(keys[i],1)) { result=1; break; }
    }
    if(!result&&!stopping&&hold_ms) {
        struct timespec delay={hold_ms/1000,(long)(hold_ms%1000)*1000000};
        while(!stopping&&nanosleep(&delay,&delay)<0) {
            if(errno!=EINTR) { result=1; break; }
        }
    }
    while(pressed) if(key(keys[--pressed],0)) result=1;
    close(fd); return result;
}
