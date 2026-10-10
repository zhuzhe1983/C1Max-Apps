// Explicit live-server QA. Reads private StreamPlayer configuration, never logs
// tokens/URLs and never loads or stops a television receiver.
#include "client.hpp"
#include "hls.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
struct Sessions {
    MediaClient &client;std::vector<Playback> values;
    ~Sessions(){for(auto &p:values)try{client.stop_transcode(p);}catch(...) {}}
    void stop(const Playback&p){client.stop_transcode(p);values.erase(std::remove_if(values.begin(),values.end(),[&](const Playback&v){return v.session==p.session&&std::string(v.device_id())==p.device_id();}),values.end());}
};
// QA only: a transcode endpoint may ignore Range. Keep only a TS prefix,
// terminate this exact wget child, and never lift production HTTP size limits.
// Headers AND the URL stay in mode-0600 files, not process arguments or logs.
struct PrefixRequest {
    std::string directory;int fd=-1;pid_t child=-1;
    PrefixRequest(){char path[]="/tmp/c1max-dual-prefix-XXXXXX";auto result=mkdtemp(path);if(!result)throw std::runtime_error("Cannot create private probe request");directory=result;}
    void finish(){
        if(fd>=0){close(fd);fd=-1;}
        if(child>0){
            kill(child,SIGTERM);int status=0;bool reaped=false;
            for(int n=0;n<10;++n){auto result=waitpid(child,&status,WNOHANG);if(result==child||(result<0&&errno==ECHILD)){reaped=true;break;}usleep(10000);}
            if(!reaped){kill(child,SIGKILL);while(waitpid(child,&status,0)<0&&errno==EINTR){}}
            child=-1;
        }
    }
    ~PrefixRequest(){finish();unlink((directory+"/config").c_str());unlink((directory+"/url").c_str());rmdir(directory.c_str());}
};
static std::string bounded_media_sample(MediaClient&client,const std::string&url){
    if(c1::origin(url)!=c1::origin(client.config.at("base")))throw std::runtime_error("Cross-server probe URL refused");
    auto token=client.config.at("token").get<std::string>();if(token.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Invalid probe authorization");
    PrefixRequest request;
    c1::save_private(request.directory+"/config","check_certificate = on\nmax_redirect = 0\ntries = 1\ntimeout = 8\nheader = Range: bytes=0-18799\nheader = X-Emby-Token: "+token+"\n");
    c1::save_private(request.directory+"/url",url+"\n");
    int pipe[2];if(pipe2(pipe,O_CLOEXEC))throw std::runtime_error("Cannot create probe stream pipe");request.fd=pipe[0];
    std::vector<std::string> args={"wget","--config="+request.directory+"/config","--input-file="+request.directory+"/url","--no-netrc","--no-hsts","--no-cookies","--no-proxy","--quiet","--output-document=-","--ca-certificate="+c1::root()+"/shared/ca-certificates.crt"};
    std::vector<char*> argv;for(auto &arg:args)argv.push_back(arg.data());argv.push_back(nullptr);
    pid_t parent=getpid();request.child=fork();
    if(!request.child){
        prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);
        int null=open("/dev/null",O_RDWR);if(null<0)_exit(1);dup2(null,0);dup2(null,2);close(null);
        dup2(pipe[1],1);close(pipe[0]);close(pipe[1]);execvp("wget",argv.data());_exit(127);
    }
    close(pipe[1]);if(request.child<0)throw std::runtime_error("Cannot start bounded probe request");
    if(fcntl(request.fd,F_SETFL,O_NONBLOCK)<0)throw std::runtime_error("Cannot configure probe request pipe");
    std::string bytes;bytes.reserve(18800);auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    bool eof=false;while(bytes.size()<18800&&!eof){
        if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("Bounded media sample timed out");
        pollfd readable{request.fd,POLLIN,0};int ready=poll(&readable,1,100);if(ready<0){if(errno==EINTR)continue;throw std::runtime_error("Probe stream poll failed");}if(!ready)continue;
        char block[4096];auto count=read(request.fd,block,std::min(sizeof block,size_t(18800)-bytes.size()));
        if(count>0)bytes.append(block,size_t(count));else if(count==0)eof=true;else if(errno!=EAGAIN&&errno!=EINTR)throw std::runtime_error("Probe stream read failed");
    }
    request.finish();if(!hls::transport_stream(bytes))throw std::runtime_error("Server did not provide a bounded MPEG-TS sample");return bytes;
}
static size_t sample(MediaClient&client,const Playback&p,size_t number){
    auto url=p.hls_url;auto playlist=hls::parse(client.fetch(url));
    for(int level=0;!playlist.variant.empty()&&level<3;++level){url=hls::resolve(url,playlist.variant);playlist=hls::parse(client.fetch(url));}
    if(playlist.segments.empty())throw std::runtime_error("No bounded HLS media segments");
    return bounded_media_sample(client,hls::resolve(url,playlist.segments[std::min(number,playlist.segments.size()-1)].uri)).size();
}
int main(int argc,char**argv){
    if(argc>2){std::cerr<<"Usage: c1max-streamplayer-dual-probe [item-id]\n";return 2;}
    try{
        c1::reset_requests(12000);MediaClient client;client.load();if(!client.ready())throw std::runtime_error("Private StreamPlayer login is required");Sessions sessions{client,{}};
        std::string item=argc==2?argv[1]:"";
        if(item.empty())for(auto&library:client.libraries()){if(library.value("CollectionType",std::string())=="music")continue;auto list=client.items(library.at("Id"));if(!list.at("Items").empty()){item=list["Items"][0].at("Id");break;}}
        if(item.empty())throw std::runtime_error("No video available");
        StreamOptions local,tv;tv.television=true;
        auto first=client.playback(item,0,local);sessions.values.push_back(first);
        auto remote=client.playback(item,0,tv);sessions.values.push_back(remote);
        if(first.session==remote.session||std::string(first.device_id())==remote.device_id())throw std::runtime_error("Server did not isolate local and TV sessions");
        std::cout<<"Both HLS outputs valid; local segment bytes="<<sample(client,first,0)<<", TV segment bytes="<<sample(client,remote,0)<<std::endl;
        sessions.stop(first);std::cout<<"Local session stopped; TV remains readable bytes="<<sample(client,remote,1)<<std::endl;
        auto second=client.playback(item,0,local);sessions.values.push_back(second);sample(client,second,0);
        sessions.stop(remote);std::cout<<"TV session stopped; local remains readable bytes="<<sample(client,second,1)<<std::endl;
        std::cout<<"PASS isolated dual-output negotiation and exact-session cleanup (network only; no display/sync claim)"<<std::endl;
    }catch(const std::exception&error){std::cerr<<error.what()<<std::endl;return 1;}return 0;
}
