#include "../game_cast.hpp"
#include "../cast.hpp"
#include "../output_mode.hpp"
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
using namespace std::chrono_literals;
namespace {
std::atomic<int> stage{0}; // 0 queued, 1 playing, 2 buffering, 3 paused, 4 failed.
std::atomic<int> loads{0},stops{0};
std::atomic<bool> connected{true};
casting::OutputMode preference=casting::OutputMode::Local;
struct Bridge {
    int listener=-1;unsigned port=0;std::atomic<int> client{-1};std::thread thread;
    std::atomic<bool> stopping{false},reject{false},stall{false};
    std::atomic<int> video_frames{0},audio_packets{0},red_pixels{0};
    Bridge(){
        listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);int yes=1;setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        assert(bind(listener,(sockaddr*)&address,sizeof address)==0);socklen_t n=sizeof address;assert(getsockname(listener,(sockaddr*)&address,&n)==0);port=ntohs(address.sin_port);assert(listen(listener,2)==0);
        thread=std::thread([this]{run();});
    }
    ~Bridge(){stopping=true;int c=client;if(c>=0)shutdown(c,SHUT_RDWR);shutdown(listener,SHUT_RDWR);close(listener);if(thread.joinable())thread.join();}
    bool read(int fd,void *data,size_t count){auto *p=(char*)data;while(count&&!stopping){ssize_t n=recv(fd,p,count,0);if(n<=0)return false;p+=n;count-=size_t(n);}return !count;}
    bool reply(int fd,const std::string &text){return send(fd,text.data(),text.size(),0)==ssize_t(text.size());}
    void run(){
        while(!stopping){int fd=accept(listener,nullptr,nullptr);if(fd<0)break;client=fd;
#ifdef SO_NOSIGPIPE
            int yes=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes);
#endif
            std::string hello;char c;while(hello.size()<4096&&read(fd,&c,1)&&c!='\n')hello+=c;
            auto parsed=casting::Json::parse(hello,nullptr,false);assert(parsed.is_object());assert(parsed["format"]=="rgb565le");
            if(reject){reply(fd,"{\"ok\":false}\n");client=-1;close(fd);continue;}
            auto answer=casting::Json{{"ok",true},{"protocol","c1max-game-v1"},{"ready",true},{"url","http://127.0.0.1:28767/session/abc/stream.m3u8"}}.dump()+"\n";
            reply(fd,answer);
            while(!stopping){
                if(stall){std::this_thread::sleep_for(50ms);continue;}
                unsigned char header[8];if(!read(fd,header,8))break;unsigned size=(unsigned(header[4])<<24)|(unsigned(header[5])<<16)|(unsigned(header[6])<<8)|header[7];
                assert(!header[1]&&!header[2]&&!header[3]);assert(size<=153600);
                std::vector<uint8_t> data(size);if(!read(fd,data.data(),size))break;
                if(header[0]=='V'){assert(size==153600);++video_frames;if(data[0]==0&&data[1]==0xf8)++red_pixels;}
                else if(header[0]=='A'){assert(size<=16384&&size%4==0);++audio_packets;}
                else assert(header[0]=='P'&&size==0);
                if(!reply(fd,answer))break;
            }
            client=-1;close(fd);
        }
    }
};
void pump(unsigned ms){
    std::array<uint16_t,256*240> pixels;pixels.fill(0x7c00);std::array<int16_t,441*2> pcm{};
    const auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);
    while(std::chrono::steady_clock::now()<until){if(game_cast::video_due())game_cast::frame_rgb555(pixels.data(),256,240,512);game_cast::audio(pcm.data(),441,2,44100);std::this_thread::sleep_for(10ms);}
}
}
namespace casting {
OutputMode output_mode(const std::string &){return preference;}
Json status(){
    const char *name=stage==1?"playing":stage==2?"buffering":stage==3?"paused":"idle";
    return Json{{"ok",true},{"connected",connected.load()},{"device",{{"host","127.0.0.1"}}},{"owner",stage==0?"old-app":"nes"},{"content_id",stage==0?6:7},{"state",name},{"error",stage==4?"receiver failed":""}};
}
Json load(const std::string&,const std::string&,const std::string&,const std::string&,bool,const std::string&,double){++loads;return Json{{"ok",true},{"accepted",true},{"accepted_id",7}};}
void detach(const std::string&){++stops;}
}
int main(){
    char temporary[]="/tmp/c1-game-cast-test-XXXXXX";auto dir=mkdtemp(temporary);assert(dir);std::string cast=std::string(dir)+"/cast";assert(!mkdir(cast.c_str(),0700));assert(!setenv("C1_APPS_DATA",dir,1));
    Bridge bridge;auto path=cast+"/game-bridge.json";{std::ofstream f(path);f<<casting::Json{{"host","127.0.0.1"},{"port",bridge.port},{"token",std::string(64,'a')}}.dump();}chmod(path.c_str(),0600);
    game_cast::start("nes","Local");pump(100);assert(loads==0&&game_cast::local_video()&&game_cast::local_audio());game_cast::stop();
    preference=casting::OutputMode::Remote;game_cast::start("nes","Queued");pump(1800);
    assert(loads==1&&stops==0);assert(game_cast::local_video()&&game_cast::local_audio()); // old owner is normal while queued.
    stage=1;pump(900);assert(!game_cast::local_video()&&!game_cast::local_audio());
    stage=2;pump(900);assert(!game_cast::local_video()&&!game_cast::local_audio());
    stage=3;pump(900);assert(!game_cast::local_video()&&!game_cast::local_audio());
    stage=4;pump(1000);assert(game_cast::local_video()&&game_cast::local_audio());assert(!game_cast::message().empty());game_cast::stop();assert(!game_cast::local_audio()); // Explicit teardown must never unmute queued PCM.
    assert(bridge.video_frames>30&&bridge.red_pixels==bridge.video_frames&&bridge.audio_packets>10);
    preference=casting::OutputMode::Both;stage=1;game_cast::start("nes","Both");pump(1000);assert(game_cast::local_video()&&!game_cast::local_audio());
    // A dead TCP peer cannot block the emulation/capture thread indefinitely.
    bridge.stall=true;auto before=std::chrono::steady_clock::now();pump(4700);auto elapsed=std::chrono::steady_clock::now()-before;
    assert(elapsed<5200ms);assert(game_cast::local_video()&&game_cast::local_audio());game_cast::stop();bridge.stall=false;
    bridge.reject=true;stage=0;game_cast::start("nes","Rejected");pump(400);assert(game_cast::local_video()&&game_cast::local_audio());assert(game_cast::message().find("rejected")!=std::string::npos);game_cast::stop();
    unlink(path.c_str());rmdir(cast.c_str());rmdir(dir);
    std::cout<<"PASS game cast: delayed ownership, buffering/pause, local/remote/both, RGB565, bounded queues, fallback and cleanup\n";
}
