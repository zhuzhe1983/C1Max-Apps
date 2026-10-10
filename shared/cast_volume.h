#ifndef C1_CAST_VOLUME_H
#define C1_CAST_VOLUME_H
/* Best-effort notification to an already running cast session. Never starts a
 * service or waits on a television in the physical-key handler. */
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static inline void c1_cast_volume_step(double delta, int mute) {
    const char *data=getenv("C1_APPS_DATA");
    struct sockaddr_un addr;
    memset(&addr,0,sizeof addr);addr.sun_family=AF_UNIX;
    int n=snprintf(addr.sun_path,sizeof addr.sun_path,"%s/cast/control.sock",data&&*data?data:"/storage/apps/data");
    if(n<0||(size_t)n>=sizeof addr.sun_path)return;
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(fd<0)return;
    if(connect(fd,(struct sockaddr*)&addr,sizeof addr)==0){
        char request[160];n=snprintf(request,sizeof request,"{\"action\":\"volume_step\",\"owner\":\"global-volume\",\"value\":%.6f,\"mute\":%s}",delta,mute?"true":"false");
        if(n>0&&(size_t)n<sizeof request)send(fd,request,n,MSG_NOSIGNAL);
    }
    close(fd);
}
#endif
