#include "cast.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
namespace casting {
static std::string path(){auto p=getenv("C1_APPS_DATA");return std::string(p&&*p?p:"/storage/apps/data")+"/cast/control.sock";}
static int connect_local(){sockaddr_un a{};a.sun_family=AF_UNIX;auto p=path();if(p.size()>=sizeof a.sun_path)return -1;strcpy(a.sun_path,p.c_str());int fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(fd>=0&&connect(fd,(sockaddr*)&a,sizeof a)){close(fd);return -1;}return fd;}
static void launch(){
    auto p=getenv("C1_APPS_ROOT");std::string bin=std::string(p&&*p?p:"/storage/apps/current")+"/shared/c1max-castd";
    // Settings ships its own daemon so a per-app update works on an older
    // runtime. Prefer that immutable store version over the shared fallback.
    std::string settings=std::string(p&&*p?p:"/storage/apps/current")+"/settings/c1max-castd";
    if(!access(settings.c_str(),X_OK))bin=settings;
    if(access(bin.c_str(),X_OK))return;pid_t child=fork();if(child<0)return;
    if(!child){if(setsid()<0)_exit(1);pid_t daemon=fork();if(daemon<0)_exit(1);if(daemon)_exit(0);
        int null=open("/dev/null",O_RDWR);if(null<0)_exit(1);for(int i=0;i<3;i++)dup2(null,i);
        long lim=sysconf(_SC_OPEN_MAX);if(lim<0||lim>65536)lim=65536;for(int i=3;i<lim;i++)close(i);
        execl(bin.c_str(),bin.c_str(),"--daemon",(char*)nullptr);_exit(127);}
    while(waitpid(child,nullptr,0)<0&&errno==EINTR){}
}
Json call(const Json &req,bool start){
    int fd=connect_local();if(fd<0&&start){launch();for(int i=0;i<20&&fd<0;i++){usleep(20000);fd=connect_local();}}
    auto fail=[](){return Json{{"ok",false},{"connected",false},{"error","投屏服务未连接"}};};if(fd<0)return fail();
    auto s=req.dump();if(s.size()>16384){close(fd);return fail();}
    if(send(fd,s.data(),s.size(),MSG_NOSIGNAL)!=(ssize_t)s.size()){close(fd);return fail();}
    pollfd p{fd,POLLIN,0};char out[49152];int r=poll(&p,1,200);ssize_t n=r>0?recv(fd,out,sizeof out,MSG_TRUNC):-1;close(fd);
    if(n<=0||n>=(ssize_t)sizeof out)return fail();auto j=Json::parse(out,out+n,nullptr,false);return j.is_object()?j:fail();
}
Json status(){return call({{"action","status"}});}
bool selected(){return status().value("connected",false);}
Json load(const std::string&o,const std::string&u,const std::string&t,const std::string&m,bool live,const std::string&i,double p){
    return call({{"action","load"},{"owner",o},{"url",u},{"title",t},{"mime",m},{"live",live},{"image",i},{"position",p}});
}
Json command(const std::string&o,const std::string&a,double v){return call({{"action",a},{"owner",o},{"value",v}});}
void detach(const std::string&o){command(o,"stop");}
}
