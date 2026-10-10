#include "cast_relay.hpp"
#include "api.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <future>
#include <poll.h>
#include <regex>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
namespace bili {
namespace {
using Clock=std::chrono::steady_clock;
struct Fd { int n=-1; explicit Fd(int fd=-1):n(fd){} ~Fd(){if(n>=0)close(n);} };
struct Download {
    pid_t pid=-1;std::string dir;
    ~Download(){if(pid>0){kill(pid,SIGKILL);while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}}if(!dir.empty()){unlink((dir+"/config").c_str());unlink((dir+"/url").c_str());rmdir(dir.c_str());}}
};
std::string lower(std::string s){for(auto&c:s)c=char(std::tolower(static_cast<unsigned char>(c)));return s;}
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return {};auto b=s.find_last_not_of(" \t\r");return s.substr(a,b-a+1);}
bool wait(int fd,short events,const std::atomic<bool>&stop,Clock::time_point deadline){while(!stop&&Clock::now()<deadline){pollfd p{fd,events,0};int n=poll(&p,1,100);if(n>0&&(p.revents&(events|POLLHUP)))return true;if(n<0&&errno!=EINTR)return false;}return false;}
bool send_bytes(int fd,const char*data,size_t count,const std::atomic<bool>&stop){auto deadline=Clock::now()+std::chrono::seconds(12);while(count&&!stop){auto n=send(fd,data,count,MSG_NOSIGNAL);if(n>0){data+=n;count-=size_t(n);deadline=Clock::now()+std::chrono::seconds(12);}else if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)){if(!wait(fd,POLLOUT,stop,deadline))return false;}else return false;}return !count;}
void reply(int fd,int code,const char*reason,const std::atomic<bool>&stop){std::string s="HTTP/1.1 "+std::to_string(code)+" "+reason+"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";send_bytes(fd,s.data(),s.size(),stop);}
// GNU wget checks the response Range against --start-pos. A suffix range
// needs the full size first; HEAD is used only if no preceding GET cached it.
uint64_t upstream_size(const std::string&url,const std::atomic<bool>&stop){
    Download dl;char temp[]="/tmp/c1-bili-relay-XXXXXX";auto dir=mkdtemp(temp);if(!dir)throw std::runtime_error("media probe failed");dl.dir=dir;
    c1::save_private(dl.dir+"/config","check_certificate = on\nmax_redirect = 0\ntries = 1\ntimeout = 10\nheader = Referer: https://www.bilibili.com/\nheader = User-Agent: Mozilla/5.0\nheader = Accept-Encoding: identity\n");c1::save_private(dl.dir+"/url",url+"\n");
    int pipes[2];if(pipe2(pipes,O_CLOEXEC))throw std::runtime_error("media probe failed");Fd in(pipes[0]),out(pipes[1]),null(open("/dev/null",O_WRONLY|O_CLOEXEC));if(null.n<0)throw std::runtime_error("media probe failed");
    std::vector<std::string> args={"wget","--config="+dl.dir+"/config","--no-netrc","--no-hsts","--no-cookies","--no-proxy","--server-response","--method=HEAD","--output-document=/dev/null","--ca-certificate="+c1::root()+"/shared/ca-certificates.crt","--input-file="+dl.dir+"/url"};std::vector<char*>argv;for(auto&a:args)argv.push_back(a.data());argv.push_back(nullptr);auto parent=getpid();dl.pid=fork();
    if(!dl.pid){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);dup2(null.n,1);dup2(out.n,2);close(in.n);close(out.n);execvp("wget",argv.data());_exit(127);}if(dl.pid<0)throw std::runtime_error("media probe failed");close(out.n);out.n=-1;fcntl(in.n,F_SETFL,O_NONBLOCK);
    auto deadline=Clock::now()+std::chrono::seconds(15);std::string headers;char b[2048];while(headers.size()<16384){if(!wait(in.n,POLLIN,stop,deadline))throw std::runtime_error("media probe failed");auto n=read(in.n,b,sizeof b);if(n<0&&(errno==EINTR||errno==EAGAIN))continue;if(n<=0)break;headers.append(b,n);}
    int status=0;while(waitpid(dl.pid,&status,WNOHANG)!=dl.pid){if(stop||Clock::now()>deadline)throw std::runtime_error("media probe failed");usleep(10000);}dl.pid=-1;
    std::smatch m;if(status||headers.size()>=16384||!std::regex_search(headers,m,std::regex("HTTP/[0-9.]+ 200")))throw std::runtime_error("media probe failed");
    headers=lower(headers);if(!std::regex_search(headers,m,std::regex("\\n[ \t]*content-length:[ \t]*([0-9]{1,16})[\r\n]")))throw std::runtime_error("media size unavailable");auto n=std::stoull(m[1]);if(!n||n>8ULL*1024*1024*1024)throw std::runtime_error("media size unavailable");return n;
}
std::string token(){Fd fd(open("/dev/urandom",O_RDONLY|O_CLOEXEC));std::array<unsigned char,24>b{};size_t n=0;while(n<b.size()){ssize_t r=fd.n<0?-1:read(fd.n,b.data()+n,b.size()-n);if(r<0&&errno==EINTR)continue;if(r<=0)throw std::runtime_error("无法生成投屏地址");n+=size_t(r);}std::string s;for(auto c:b){s+="0123456789abcdef"[c>>4];s+="0123456789abcdef"[c&15];}return s;}
}
CastRelay::~CastRelay(){stop();}
std::string CastRelay::start(const std::string&upstream,const std::string&receiver){
    stop();if(!media_url(upstream)||upstream.find('\0')!=std::string::npos)throw std::runtime_error("无效的 B 站媒体源");
    sockaddr_in remote{};remote.sin_family=AF_INET;remote.sin_port=htons(9);
    if(inet_pton(AF_INET,receiver.c_str(),&remote.sin_addr)!=1)throw std::runtime_error("接收器地址无效，请重新连接");
    Fd route(socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0));sockaddr_in local{};socklen_t size=sizeof local;
    if(route.n<0||connect(route.n,reinterpret_cast<sockaddr*>(&remote),sizeof remote)||getsockname(route.n,reinterpret_cast<sockaddr*>(&local),&size)||!local.sin_addr.s_addr)throw std::runtime_error("无法确定投屏网络，请检查 Wi-Fi");
    char ip[INET_ADDRSTRLEN];if(!inet_ntop(AF_INET,&local.sin_addr,ip,sizeof ip))throw std::runtime_error("无法确定本机网络地址");
    listener_=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);local.sin_port=0;
    if(listener_<0||bind(listener_,reinterpret_cast<sockaddr*>(&local),sizeof local)||listen(listener_,4)||getsockname(listener_,reinterpret_cast<sockaddr*>(&local),&size)){stop();throw std::runtime_error("无法启动投屏视频转发");}
    upstream_=upstream;if(upstream_.rfind("http://",0)==0)upstream_.replace(0,4,"https");
    request_path_="/"+token()+"/video.mp4";peer_=receiver;size_=0;stopping_=false;
    try{thread_=std::thread([this]{serve();});}catch(...){stop();throw;}
    return "http://"+std::string(ip)+":"+std::to_string(ntohs(local.sin_port))+request_path_;
}
void CastRelay::stop(){stopping_=true;if(thread_.joinable())thread_.join();if(listener_>=0)close(listener_);listener_=-1;upstream_.clear();request_path_.clear();peer_.clear();}
void CastRelay::serve(){
    std::vector<std::future<void>> workers;
    while(!stopping_){
        for(auto it=workers.begin();it!=workers.end();)if(it->wait_for(std::chrono::seconds(0))==std::future_status::ready){it->get();it=workers.erase(it);}else ++it;
        pollfd p{listener_,POLLIN,0};if(poll(&p,1,100)<=0)continue;
        sockaddr_in addr{};socklen_t size=sizeof addr;int fd=accept4(listener_,reinterpret_cast<sockaddr*>(&addr),&size,SOCK_CLOEXEC|SOCK_NONBLOCK);if(fd<0)continue;
        char ip[INET_ADDRSTRLEN];bool allowed=inet_ntop(AF_INET,&addr.sin_addr,ip,sizeof ip)&&peer_==ip;
        if(!allowed||workers.size()>=3){reply(fd,allowed?503:403,allowed?"Busy":"Forbidden",stopping_);close(fd);continue;}
        try{workers.push_back(std::async(std::launch::async,[this,fd]{Fd client(fd);try{transfer(fd);}catch(...){reply(fd,502,"Upstream unavailable",stopping_);}}));}catch(...){close(fd);}
    }
    for(auto&w:workers)w.wait();
}
void CastRelay::transfer(int fd){
    std::string request;char buffer[16384];auto deadline=Clock::now()+std::chrono::seconds(5);size_t split;
    while((split=request.find("\r\n\r\n"))==std::string::npos){if(request.size()>=4096){reply(fd,431,"Headers too large",stopping_);return;}if(!wait(fd,POLLIN,stopping_,deadline))return;auto n=recv(fd,buffer,std::min(sizeof buffer,size_t(4096-request.size())),0);if(n<0&&(errno==EINTR||errno==EAGAIN))continue;if(n<=0)return;request.append(buffer,n);}
    auto end=request.find("\r\n");auto line=request.substr(0,end);auto sp=line.find(' '),sp2=line.find(' ',sp==std::string::npos?sp:sp+1);
    if(sp==std::string::npos||sp2==std::string::npos){reply(fd,400,"Bad request",stopping_);return;}
    auto method=line.substr(0,sp),path=line.substr(sp+1,sp2-sp-1);bool head=method=="HEAD";
    if(method!="GET"&&!head){reply(fd,405,"Method not allowed",stopping_);return;}if(path!=request_path_){reply(fd,404,"Not found",stopping_);return;}
    std::string range;for(size_t p=end+2;p<split;){auto e=request.find("\r\n",p);auto header=request.substr(p,e-p);auto c=header.find(':');if(c==std::string::npos){reply(fd,400,"Bad header",stopping_);return;}auto key=lower(trim(header.substr(0,c))),value=trim(header.substr(c+1));if(key=="range"){if(!range.empty()||!std::regex_match(value,std::regex("bytes=([0-9]{1,16}-[0-9]{0,16}|-[0-9]{1,16})"))){reply(fd,416,"Invalid range",stopping_);return;}range=value;}if(key=="transfer-encoding"||(key=="content-length"&&value!="0")){reply(fd,400,"Request body refused",stopping_);return;}p=e+2;}
    if(range.rfind("bytes=-",0)==0){auto total=size_.load();if(!total){total=upstream_size(upstream_,stopping_);size_=total;}auto count=std::stoull(range.substr(7));if(!count){reply(fd,416,"Invalid range",stopping_);return;}range="bytes="+std::to_string(total-std::min<uint64_t>(total,count))+"-"+std::to_string(total-1);}
    Download dl;char temp[]="/tmp/c1-bili-relay-XXXXXX";auto dir=mkdtemp(temp);if(!dir){reply(fd,503,"Busy",stopping_);return;}dl.dir=dir;
    std::string config="check_certificate = on\nmax_redirect = 0\ntries = 1\ntimeout = 10\nheader = Referer: https://www.bilibili.com/\nheader = User-Agent: Mozilla/5.0\nheader = Accept-Encoding: identity\n";if(!range.empty())config+="header = Range: "+range+"\n";
    c1::save_private(dl.dir+"/config",config);c1::save_private(dl.dir+"/url",upstream_+"\n");
    int pipes[2];if(pipe2(pipes,O_CLOEXEC)){reply(fd,503,"Busy",stopping_);return;}Fd in(pipes[0]),out(pipes[1]);int logs[2];if(pipe2(logs,O_CLOEXEC)){reply(fd,503,"Busy",stopping_);return;}Fd log_in(logs[0]),log_out(logs[1]);
    std::vector<std::string> args={"wget","--config="+dl.dir+"/config","--no-netrc","--no-hsts","--no-cookies","--no-proxy","--quiet","--server-response","--output-document=-","--ca-certificate="+c1::root()+"/shared/ca-certificates.crt","--input-file="+dl.dir+"/url"};
    if(!range.empty()&&range[6]!='-')args.push_back("--start-pos="+range.substr(6,range.find('-',6)-6));
    std::vector<char*> argv;for(auto&a:args)argv.push_back(a.data());argv.push_back(nullptr);auto parent=getpid();dl.pid=fork();
    if(!dl.pid){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);dup2(out.n,1);dup2(log_out.n,2);close(in.n);close(out.n);close(log_in.n);close(log_out.n);execvp("wget",argv.data());_exit(127);}if(dl.pid<0){reply(fd,503,"Busy",stopping_);return;}close(out.n);out.n=-1;close(log_out.n);log_out.n=-1;fcntl(in.n,F_SETFL,O_NONBLOCK);fcntl(log_in.n,F_SETFL,O_NONBLOCK);
    // Wget omits --save-headers when resuming. Read server-response from a
    // separate bounded private pipe; signed URLs/cookies never leave this process.
    std::string diagnostic;deadline=Clock::now()+std::chrono::seconds(15);bool body_ready=false;
    while(!stopping_&&Clock::now()<deadline&&!body_ready){
        pollfd descriptors[2]={{in.n,POLLIN,0},{log_in.n,POLLIN,0}};if(poll(descriptors,2,100)<0&&errno!=EINTR)break;
        ssize_t n;while((n=read(log_in.n,buffer,sizeof buffer))>0){diagnostic.append(buffer,n);if(diagnostic.size()>16384){reply(fd,502,"Invalid media response",stopping_);return;}}
        body_ready=descriptors[0].revents&(POLLIN|POLLHUP);
    }
    if(!body_ready){reply(fd,502,"Upstream unavailable",stopping_);return;}
    std::string response;bool header=false;for(size_t p=0;p<diagnostic.size();){auto e=diagnostic.find('\n',p);if(e==std::string::npos)break;auto h=trim(diagnostic.substr(p,e-p));if(h.rfind("HTTP/",0)==0){response=h+"\r\n";header=true;}else if(header&&h.find(':')!=std::string::npos)response+=h+"\r\n";p=e+1;}response+="\r\n";
    split=response.find("\r\n\r\n");if(split==std::string::npos){reply(fd,502,"Upstream unavailable",stopping_);return;}
    end=response.find("\r\n");line=response.substr(0,end);sp=line.find(' ');int code=0;if(sp!=std::string::npos&&line.size()>=sp+4)try{code=std::stoi(line.substr(sp+1,3));}catch(...){}
    if(code!=200&&code!=206){reply(fd,502,"Upstream unavailable",stopping_);return;}
    // Forward only framing headers, never cookies, signed URLs or redirects.
    std::string length,content_range;bool chunked=false;
    for(size_t p=end+2;p<split;){auto e=response.find("\r\n",p);auto h=response.substr(p,e-p);auto c=h.find(':');if(c!=std::string::npos){auto k=lower(trim(h.substr(0,c))),v=trim(h.substr(c+1));if(k=="content-length"&&std::regex_match(v,std::regex("[0-9]{1,16}")))length=v;if(k=="content-range"&&std::regex_match(v,std::regex("bytes [0-9]{1,16}-[0-9]{1,16}/[0-9]{1,16}")))content_range=v;if(k=="transfer-encoding")chunked=true;}p=e+2;}
    if(chunked||length.empty()||(code==206&&content_range.empty())){reply(fd,502,"Invalid media response",stopping_);return;}
    auto expected=std::stoull(length);if(code==200)size_=expected;else size_=std::stoull(content_range.substr(content_range.find('/')+1));if(expected>8ULL*1024*1024*1024){reply(fd,502,"Media too large",stopping_);return;}
    std::string headers="HTTP/1.1 "+std::to_string(code)+(code==206?" Partial Content\r\n":" OK\r\n")+"Content-Type: video/mp4\r\nContent-Length: "+length+"\r\nAccept-Ranges: bytes\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\nConnection: close\r\n";
    if(!content_range.empty())headers+="Content-Range: "+content_range+"\r\n";headers+="\r\n";if(!send_bytes(fd,headers.data(),headers.size(),stopping_)||head)return;
    auto body=response.substr(split+4);size_t initial=std::min<uint64_t>(body.size(),expected);if(initial&&!send_bytes(fd,body.data(),initial,stopping_))return;uint64_t remaining=expected-initial;
    deadline=Clock::now()+std::chrono::seconds(15);while(remaining&&!stopping_){if(!wait(in.n,POLLIN,stopping_,deadline))return;auto n=read(in.n,buffer,std::min<uint64_t>(sizeof buffer,remaining));if(n<0&&(errno==EINTR||errno==EAGAIN))continue;if(n<=0)return;if(!send_bytes(fd,buffer,size_t(n),stopping_))return;remaining-=size_t(n);deadline=Clock::now()+std::chrono::seconds(15);}
}
}
