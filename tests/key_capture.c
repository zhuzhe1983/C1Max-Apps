/* Read-only bounded physical-key capture for on-device mapping verification. */
#include <linux/input.h>
#include <poll.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
static long now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec;}
int main(int argc,char **argv){
    int duration=argc>1?atoi(argv[1]):180;if(duration<1||duration>600)return 2;
    struct pollfd fds[2]={{.fd=open("/dev/input/event0",O_RDONLY|O_NONBLOCK),.events=POLLIN},{.fd=open("/dev/input/event1",O_RDONLY|O_NONBLOCK),.events=POLLIN}};
    if(fds[0].fd<0||fds[1].fd<0)return 1;long end=now()+duration;
    puts("CAPTURE_READY event0,event1");fflush(stdout);
    while(now()<end){if(poll(fds,2,250)<0)break;
        for(int i=0;i<2;i++){struct input_event e;while(read(fds[i].fd,&e,sizeof e)==sizeof e){if(e.type==EV_KEY){printf("event%d code=%u value=%d time=%ld.%06ld\n",i,e.code,e.value,(long)e.time.tv_sec,(long)e.time.tv_usec);fflush(stdout);}}}
    }
    close(fds[0].fd);close(fds[1].fd);puts("CAPTURE_DONE");return 0;
}
