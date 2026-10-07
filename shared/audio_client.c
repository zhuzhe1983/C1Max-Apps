#define _GNU_SOURCE
#include "audio_client.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *data(void){const char *p=getenv("C1_APPS_DATA");return p&&*p?p:"/storage/apps/data";}
int c1_audio_socket_path(char *out,unsigned size){
    int n=snprintf(out,size,"%s/audio/service.sock",data());return n>0&&(unsigned)n<size?0:-1;
}
static int connect_audio(void){
    struct sockaddr_un addr;memset(&addr,0,sizeof addr);addr.sun_family=AF_UNIX;
    if(c1_audio_socket_path(addr.sun_path,sizeof addr.sun_path))return -1;
    int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd<0)return -1;
    if(connect(fd,(struct sockaddr*)&addr,sizeof addr)){close(fd);return -1;}return fd;
}
static void start_audio(int owner){
    const char *root=getenv("C1_APPS_ROOT");if(!root||!*root)root="/storage/apps/current";
    static const char *names[]={"shared","piano","airtune","streamplayer"};
    if(owner<0||owner>3)return;
    char executable[512];int n=snprintf(executable,sizeof executable,"%s/%s/c1max-audio",root,names[owner]);
    if(n<0||n>=(int)sizeof executable||access(executable,X_OK))return;
    pid_t child=fork();if(child<0)return;
    if(!child){
        if(setsid()<0)_exit(1);
        pid_t daemon=fork();if(daemon<0)_exit(1);if(daemon)_exit(0);
        signal(SIGPIPE,SIG_IGN);int null=open("/dev/null",O_RDWR);
        if(null<0)_exit(1);for(int i=0;i<3;i++)dup2(null,i);if(null>2)close(null);
        /* Close inherited framebuffer/input/audio handles before exec. */
        long limit=sysconf(_SC_OPEN_MAX);if(limit<0||limit>65536)limit=65536;
        for(int i=3;i<limit;i++)close(i);
        execl(executable,executable,"--daemon",(char*)NULL);_exit(127);
    }
    while(waitpid(child,NULL,0)<0&&errno==EINTR){}
}
int c1_audio_call(struct c1_audio_request *request,struct c1_audio_status *status,int start){
    struct c1_audio_status sink;if(!status)status=&sink;memset(status,0,sizeof *status);
    int fd=connect_audio();
    if(fd<0&&start){start_audio(request->owner);for(int i=0;i<20&&fd<0;i++){usleep(30000);fd=connect_audio();}}
    if(fd<0)return -1;
    request->magic=C1_AUDIO_MAGIC;
    struct pollfd wait={fd,POLLOUT,0};int ready;
    do{ready=poll(&wait,1,250);}while(ready<0&&errno==EINTR);
    if(ready<=0||!(wait.revents&POLLOUT)||send(fd,request,sizeof *request,MSG_NOSIGNAL)!=(ssize_t)sizeof *request){close(fd);return -1;}
    wait.events=POLLIN;wait.revents=0;
    do{ready=poll(&wait,1,750);}while(ready<0&&errno==EINTR);
    ssize_t count=ready>0&&(wait.revents&POLLIN)?recv(fd,status,sizeof *status,MSG_TRUNC):-1;
    close(fd);
    if(count!=(ssize_t)sizeof *status||status->magic!=C1_AUDIO_MAGIC){memset(status,0,sizeof *status);return -1;}
    return status->result;
}
int c1_audio_get(struct c1_audio_status *status){struct c1_audio_request r={0};r.action=C1_AUDIO_STATUS;return c1_audio_call(&r,status,0);}
int c1_audio_command(int owner,int action,int value,struct c1_audio_status *status){
    struct c1_audio_request r={0};r.owner=owner;r.action=action;r.value=value;return c1_audio_call(&r,status,0);
}
int c1_audio_background_preference(int owner,int value){
    static const char *names[]={"","piano","airtune","streamplayer"};
    if(owner<1||owner>3)return -1;
    char dir[512],path[550],tmp[580];
    int n=snprintf(dir,sizeof dir,"%s/%s",data(),names[owner]);if(n<0||n>=(int)sizeof dir)return -1;
    snprintf(path,sizeof path,"%s/background-playback",dir);
    if(value<0){int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW);char b=0;if(fd>=0){if(read(fd,&b,1)!=1)b=0;close(fd);}return b=='1';}
    if(value>1)return -1;
    if(mkdir(dir,0700)&&errno!=EEXIST)return -1;
    snprintf(tmp,sizeof tmp,"%s.XXXXXX",path);int fd=mkstemp(tmp);if(fd<0)return -1;
    char b[2]={value?'1':'0','\n'};int ok=fchmod(fd,0600)==0&&write(fd,b,2)==2&&fsync(fd)==0;
    if(close(fd))ok=0;if(ok&&rename(tmp,path)==0){int d=open(dir,O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(d>=0){fsync(d);close(d);}return value;}
    unlink(tmp);return -1;
}

void c1_audio_text(char *out,unsigned size,const char *input){
    if(!size)return;size_t length=strlen(input),n=length<size?length:size-1;
    while(n&&n<length&&((unsigned char)input[n]&0xc0)==0x80)--n;
    memcpy(out,input,n);out[n]=0;
}
