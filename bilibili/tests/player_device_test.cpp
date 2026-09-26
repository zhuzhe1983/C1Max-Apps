// Device-only integration test of the real Player / original MPlayer / CDN.
// No framebuffer, keyboard or audio device access. Display submission is a sink;
// decoding, YUV FIFO, transport, slave controls and timing remain the real code.
#include "player.hpp"
#include "display.hpp"
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <unistd.h>
namespace screen {
bool playing=false,tap=false;
uint32_t tick(){return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();}
bool video_begin(){return true;}
void video_frame(const uint32_t*,int,int,int,int){}
void video_refresh(bool){}
void video_end(){}
}
int main(int argc,char**argv){
    if(argc!=2||!getenv("C1_APPS_DATA")){std::cerr<<"Set a temporary C1_APPS_DATA, then pass a BV id\n";return 2;}
    setenv("C1_BILI_SILENT","1",1);
    signal(SIGPIPE,SIG_IGN);
    std::filesystem::create_directories(c1::data()+"/bilibili");
    try{
        bili::Api api;auto detail=api.detail(argv[1]);auto source=api.stream(detail["bvid"],detail["pages"][0]["cid"]);
        if(source.duration<90)throw std::runtime_error("Choose a video longer than 90 seconds");
        bili::Player p;p.start(source);
        auto poll=[&]{p.poll();if(!p.error.empty())throw std::runtime_error(p.error);if(!p.active())throw std::runtime_error("Player exited unexpectedly");usleep(5000);};
        auto until=[&](const std::function<bool()>&ready,const char*label){auto start=screen::tick();while(!ready()){poll();if(screen::tick()-start>20000)throw std::runtime_error(label);}};
        auto settle=[&](unsigned ms){auto start=screen::tick();while(screen::tick()-start<ms)poll();};
        auto stable=[&]{settle(500);auto frames=p.frames();auto pos=p.position;settle(2200);if(!p.paused||p.frames()!=frames||std::abs(p.position-pos)>0.2)throw std::runtime_error("Pause did not hold frames/time");};
        until([&]{return p.loaded&&p.position>=3;},"Initial playback timed out");
        p.pause();stable();std::cout<<"PASS pause holds frames/time\n";
        auto frames=p.frames();p.seek(15);
        until([&]{return p.frames()>frames&&std::abs(p.position-15)<1;},"Paused seek timed out");
        stable();std::cout<<"PASS paused seek to 15s holds new frame/time\n";
        p.pause();until([&]{return p.position>=17&&p.frames()>frames+20;},"Resume timed out");
        std::cout<<"PASS resume advances frames/time\n";
        frames=p.frames();p.seek(70);until([&]{return p.frames()>frames+3&&p.position>=70&&p.position<75;},"Forward seek timed out");
        settle(1200);if(p.position<70||p.position>80)throw std::runtime_error("Forward seek did not persist");
        std::cout<<"PASS forward seek to 70s\n";
        frames=p.frames();p.seek(5);until([&]{return p.frames()>frames+3&&p.position>=5&&p.position<8;},"Backward seek timed out");
        settle(1500);if(p.position<5||p.position>12)throw std::runtime_error("Backward seek did not persist");
        std::cout<<"PASS backward seek to 5s\n";p.stop();return 0;
    }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
