#define _GNU_SOURCE
/* GNU wget subset used by unchanged C1Max applications. HTTPS is performed
   by the loopback host bridge with certificate verification. */
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
static int send_all(int fd,const void *data,size_t size){const char *p=data;while(size){ssize_t n=send(fd,p,size,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0)return -1;p+=n;size-=n;}return 0;}
static int safe(const char *value){return value&&!strpbrk(value,"\r\n");}
int main(int argc,char **argv){
    const char *url=NULL,*output="-",*method="GET",*body_file=NULL,*config=NULL;int verbose=0,redirect=10;
    char headers[8192]="",line[4096];
    for(int i=1;i<argc;i++){
        const char *a=argv[i];
        if(!strcmp(a,"-O")&&i+1<argc)output=argv[++i];
        else if(!strncmp(a,"--output-document=",18))output=a+18;
        else if(!strncmp(a,"--method=",9))method=a+9;
        else if(!strncmp(a,"--body-file=",12))body_file=a+12;
        else if(!strncmp(a,"--config=",9))config=a+9;
        else if(!strcmp(a,"--server-response")||!strcmp(a,"-S"))verbose=1;
        else if(!strncmp(a,"--header=",9)){if(!safe(a+9)||strlen(headers)+strlen(a+9)+3>=sizeof(headers))return 2;strcat(headers,a+9);strcat(headers,"\r\n");}
        else if(a[0]!='-')url=a;
    }
    if(!safe(url)||!safe(method)||(strncmp(url,"http://",7)&&strncmp(url,"https://",8))){fprintf(stderr,"wget: expected HTTP(S) URL\n");return 2;}
    if(config){FILE *f=fopen(config,"r");if(!f)return 2;while(fgets(line,sizeof(line),f)){
        line[strcspn(line,"\r\n")]=0;
        if(!strncmp(line,"header = ",9)){if(strlen(headers)+strlen(line+9)+3>=sizeof(headers)){fclose(f);return 2;}strcat(headers,line+9);strcat(headers,"\r\n");}
        if(!strncmp(line,"max_redirect = ",15))redirect=atoi(line+15);
    }fclose(f);}
    size_t body_size=0;char *body=NULL;
    if(body_file){FILE *f=fopen(body_file,"rb");if(!f)return 2;fseek(f,0,SEEK_END);long size=ftell(f);rewind(f);if(size<0||size>1048576){fclose(f);return 2;}body_size=size;body=malloc(body_size+1);if(!body||fread(body,1,body_size,f)!=body_size){fclose(f);free(body);return 2;}fclose(f);}
    const char *port=getenv("C1SIM_HTTP"),*token=getenv("C1SIM_TOKEN");if(!port||!token){free(body);return 2;}
    int fd=socket(AF_INET,SOCK_STREAM,0);struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(atoi(port))};inet_pton(AF_INET,"10.0.2.2",&address.sin_addr);
    struct timeval timeout={60,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(connect(fd,(struct sockaddr*)&address,sizeof(address))){perror("wget host bridge");free(body);close(fd);return 4;}
    char request[16384];int size=snprintf(request,sizeof(request),"POST /network HTTP/1.1\r\nHost: 10.0.2.2\r\nConnection: close\r\nX-C1Sim-Token: %s\r\nX-C1Max-URL: %s\r\nX-C1Max-Method: %s\r\nX-C1Max-Redirect: %d\r\nContent-Length: %zu\r\n%s\r\n",token,url,method,redirect,body_size,headers);
    if(size<0||size>=(int)sizeof(request)||send_all(fd,request,size)||send_all(fd,body,body_size)){free(body);close(fd);return 4;}free(body);
    char response[32768];size_t used=0;
    while(used<sizeof(response)-1){ssize_t n=recv(fd,response+used,1,0);if(n<=0){close(fd);return 4;}used++;if(used>=4&&!memcmp(response+used-4,"\r\n\r\n",4))break;}
    response[used]=0;int status=0;sscanf(response,"HTTP/%*s %d",&status);
    if(verbose)fwrite(response,1,used,stderr);
    char *length=strcasestr(response,"\r\nContent-Length:");if(!length){close(fd);return 4;}long expected=strtol(length+17,NULL,10);if(expected<0||expected>64*1024*1024){close(fd);return 4;}
    FILE *out=!strcmp(output,"-")?stdout:fopen(output,"wb");if(!out){close(fd);return 3;}
    char buffer[8192];long total=0;while(total<expected){ssize_t n=recv(fd,buffer,sizeof(buffer)<(size_t)(expected-total)?sizeof(buffer):(size_t)(expected-total),0);if(n<0&&errno==EINTR)continue;if(n<=0||fwrite(buffer,1,n,out)!=(size_t)n)break;total+=n;}
    if(out!=stdout)fclose(out);
    close(fd);
    return total==expected&&status>=200&&status<300?0:8;
}
