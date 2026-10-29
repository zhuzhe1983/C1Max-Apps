#define _GNU_SOURCE
/* MPlayer slave adapter. Decoding remains in the MIPS guest. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static volatile sig_atomic_t quitting;
static void stop(int value){(void)value;quitting=1;}
static double clock_seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static int header(char *target,size_t capacity,const char *name,const char *value){
    if(strpbrk(value,"\r\n"))return -1;
    size_t used=strlen(target);int n=snprintf(target+used,capacity-used,"%s: %s\r\n",name,value);
    return n<0||(size_t)n>=capacity-used?-1:0;
}
int main(int argc,char **argv){
    const char *path=NULL,*agent=NULL,*referer=NULL;
    char headers[8192]="",fallback_agent[4096]="",fallback_referer[4096]="";int silent=0;
    for(int i=1;i<argc;i++){
        const char *a=argv[i];
        if(!strcmp(a,"-vo")&&i+1<argc&&!strcmp(argv[i+1],"help")){puts("Available video output drivers:\n null (Y4M FIFO with C1_YUV_FIFO; otherwise audio)");return 0;}
        if(!strcmp(a,"-user-agent")&&i+1<argc)agent=argv[++i];
        else if(!strcmp(a,"-referrer")&&i+1<argc)referer=argv[++i];
        else if(!strcmp(a,"-ao")&&i+1<argc)silent=!strcmp(argv[++i],"null");
        else if(!strcmp(a,"-lavfstreamopts")&&i+1<argc){
            char *copy=strdup(argv[++i]);if(!copy)return 1;
            for(char *part=strtok(copy,",");part;part=strtok(NULL,",")){
                char *target=NULL,*value=NULL;
                if(!strncmp(part,"user_agent=",11)){target=fallback_agent;value=part+11;}
                else if(!strncmp(part,"referer=",8)){target=fallback_referer;value=part+8;}
                if(target){size_t size=strlen(value);if(size>=sizeof(fallback_agent)){free(copy);return 2;}memcpy(target,value,size+1);}
            }free(copy);
        }
        else if(!strcmp(a,"-noconfig")||!strcmp(a,"-vo")||!strcmp(a,"-cache")||!strcmp(a,"-cache-min")||!strcmp(a,"-demuxer")||!strcmp(a,"-lavfdopts")||!strcmp(a,"-vf")||!strcmp(a,"-volume")||!strcmp(a,"-osdlevel")){if(i+1<argc)i++;}
        else if(a[0]!='-')path=a;
    }
    if(!path)return 2;
    if(!agent&&*fallback_agent)agent=fallback_agent;
    if(!referer&&*fallback_referer)referer=fallback_referer;
    if((agent&&header(headers,sizeof(headers),"User-Agent",agent))||(referer&&header(headers,sizeof(headers),"Referer",referer)))return 2;
    if(getenv("C1_YUV_FIFO")){
        const char *backend=getenv("C1SIM_VIDEO");
        if(backend&&!strcmp(backend,"host")){
            execl("/usr/bin/c1max-host-video","c1max-host-video","--headers",headers,"--silent",silent?"1":"0","--",path,NULL);
            perror("video acceleration bridge");return 1;
        }
        execl("/usr/bin/c1max-video","c1max-video","--headers",headers,"--silent",silent?"1":"0","--",path,NULL);
        perror("MIPS video decoder");return 1;
    }
    int pipefd[2];if(pipe2(pipefd,O_CLOEXEC))return 1;
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGPIPE,SIG_IGN);
    pid_t parent=getpid(),child=fork();
    if(!child){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);close(pipefd[0]);dup2(pipefd[1],1);dup2(pipefd[1],2);close(pipefd[1]);execl("/usr/bin/ffplay","ffplay","-nodisp","-autoexit","-headers",headers,"--",path,NULL);_exit(127);}
    if(child<0)return 1;
    close(pipefd[1]);fcntl(pipefd[0],F_SETFL,O_NONBLOCK);setvbuf(stdout,NULL,_IOLBF,0);
    double started=clock_seconds(),pause_at=0,paused_time=0;int paused=0,ready=0,status=0;char pending[4096];size_t used=0;
    while(!quitting){
        if(waitpid(child,&status,WNOHANG)==child){child=-1;break;}
        struct pollfd fds[]={{STDIN_FILENO,POLLIN,0},{pipefd[0],POLLIN,0}};
        if(poll(fds,2,100)<0&&errno!=EINTR)break;
        if(fds[1].revents&POLLIN){char buffer[2048];ssize_t n=read(pipefd[0],buffer,sizeof(buffer));if(n>0){if(!ready&&(memmem(buffer,n,"time=",5)||memmem(buffer,n," M-A:",5))){puts("Starting playback...");ready=1;}fwrite(buffer,1,n,stdout);fflush(stdout);}}
        if(fds[0].revents&POLLIN){
            ssize_t n=read(0,pending+used,sizeof(pending)-used-1);if(n<=0)continue;used+=n;pending[used]=0;char *begin=pending,*end;
            while((end=strchr(begin,'\n'))){*end=0;
                if(!strcmp(begin,"pause")){paused=!paused;kill(child,paused?SIGSTOP:SIGCONT);if(paused)pause_at=clock_seconds();else paused_time+=clock_seconds()-pause_at;}
                else if(strstr(begin,"get_time_pos"))printf("ANS_TIME_POSITION=%.2f\n",(paused?pause_at:clock_seconds())-started-paused_time);
                else if(!strcmp(begin,"quit"))quitting=1;
                else if(strstr(begin,"seek"))puts("ANS_ERROR=seek is unavailable in this audio adapter");
                begin=end+1;
            }
            used-=begin-pending;memmove(pending,begin,used);if(used==sizeof(pending)-1)used=0;
        }
    }
    if(child>0){kill(child,SIGCONT);kill(child,SIGTERM);for(int i=0;i<20;i++){if(waitpid(child,&status,WNOHANG)==child){child=-1;break;}usleep(50000);}if(child>0){kill(child,SIGKILL);waitpid(child,&status,0);}}
    close(pipefd[0]);return quitting?0:WIFEXITED(status)?WEXITSTATUS(status):1;
}
