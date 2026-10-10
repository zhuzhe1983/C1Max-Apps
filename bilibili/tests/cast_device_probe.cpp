// Real Player/CDN/receiver integration probe. No framebuffer or keyboard access.
// Remote may sound on the selected receiver; the C1 Max decoder is always silent.
#include "player.hpp"
#include "display.hpp"
#include "cast.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
namespace {
volatile sig_atomic_t interrupted=0;
void interrupt(int){interrupted=1;}
using Clock=std::chrono::steady_clock;
struct Deadline {
    Clock::time_point end=Clock::now()+std::chrono::seconds(55);
    std::atomic<bool> done{false},cancelled{false};
    std::thread worker;
    Deadline():worker([this]{while(!done){if(interrupted||Clock::now()>=end){cancelled=true;c1::cancel_requests();break;}std::this_thread::sleep_for(std::chrono::milliseconds(40));}}){}
    ~Deadline(){done=true;worker.join();}
    void check()const{if(interrupted||cancelled||Clock::now()>=end)throw std::runtime_error("deadline");}
    int remaining_ms()const{return std::chrono::duration_cast<std::chrono::milliseconds>(end-Clock::now()).count();}
};
bool silent_child(){
    for(auto&e:std::filesystem::directory_iterator("/proc")){
        auto name=e.path().filename().string();if(name.empty()||name.find_first_not_of("0123456789")!=std::string::npos)continue;
        try{
            auto status=c1::read_file((e.path()/"status").string(),8192);auto p=status.find("\nPPid:");if(p==std::string::npos||std::stoi(status.substr(p+6))!=getpid())continue;
            auto raw=c1::read_file((e.path()/"cmdline").string(),16384);std::vector<std::string>args;
            for(size_t at=0;at<raw.size();){auto end=raw.find('\0',at);if(end==std::string::npos)break;args.push_back(raw.substr(at,end-at));at=end+1;}
            if(args.empty()||args[0].find("mplayer")==std::string::npos)continue;
            for(size_t i=1;i+1<args.size();i++)if(args[i]=="-ao")return args[i+1]=="null";
        }catch(...){}
    }return false;
}
}
namespace screen {
bool playing=false,tap=false;
uint32_t tick(){return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();}
bool video_begin(){return true;}
void video_frame(const uint32_t*,int,int,int,int){}
void video_refresh(bool){}
void video_end(){}
}
int main(int argc,char**argv){
    if(argc<2||argc>3||(std::string(argv[1])!="remote"&&std::string(argv[1])!="both")||!getenv("C1_APPS_DATA")||c1::data()=="/storage/apps/data"){
        std::cerr<<"Use an isolated C1_APPS_DATA with a connected receiver: cast-probe remote|both [BV-id]\n";return 2;
    }
    const bool both=std::string(argv[1])=="both";const char*stage="setup";
    signal(SIGPIPE,SIG_IGN);signal(SIGINT,interrupt);signal(SIGTERM,interrupt);
    setenv("C1_BILI_SILENT","1",1);unsetenv("C1_BILI_QA_LOCAL");
    try{
        Deadline deadline;
        std::filesystem::create_directories(c1::data()+"/bilibili");
        auto initial=casting::status();if(!initial.value("connected",false))throw std::runtime_error("receiver");
        if(!initial.value("owner",std::string()).empty())throw std::runtime_error("receiver busy");
        if(!casting::set_output_mode("bilibili",both?casting::OutputMode::Both:casting::OutputMode::Remote))throw std::runtime_error("preference");
        bili::Api api([&](const std::string&url,const std::vector<std::string>&headers){
            deadline.check();auto remaining=deadline.remaining_ms();if(remaining<18000)throw std::runtime_error("API budget exhausted");
            c1::reset_requests(std::min(5000,remaining-18000));return c1::http("GET",url,headers,"",&deadline.cancelled,4);
        });
        stage="public API";std::vector<std::string>ids;
        if(argc==3){auto id=bili::video_id(argv[2]);if(id!=argv[2])throw std::runtime_error("BV format");ids.push_back(id);}
        else{auto page=api.popular(1);for(auto&item:page.at("items")){ids.push_back(item.at("bvid"));if(ids.size()==3)break;}}
        bili::Stream source;bool found=false;
        for(auto&id:ids){deadline.check();try{auto detail=api.detail(id);auto stream=api.stream(id,detail.at("pages")[0].at("cid"));if(stream.duration<12)continue;source=std::move(stream);found=true;break;}catch(...){deadline.check();}}
        if(!found)throw std::runtime_error("No eligible public MP4 in bounded candidates");
        std::cout<<"PASS public MP4 selected (no account)\n"<<std::flush;
        bili::Player player;
        stage="load";player.start(source,"C1Max Bilibili cast test");
        Json remote;uint32_t last_status=0;uint64_t content_id=0;
        auto poll=[&]{
            deadline.check();player.poll();if(!player.error.empty()||!player.active())throw std::runtime_error("player unavailable");
            auto now=screen::tick();if(now-last_status>250){remote=casting::status();last_status=now;if(!remote.value("connected",false)||!remote.value("error",std::string()).empty())throw std::runtime_error("receiver unavailable");
                if(content_id&&(remote.value("owner",std::string())!="bilibili"||remote.value("content_id",uint64_t(0))!=content_id))throw std::runtime_error("receiver ownership changed");}
            if(!both&&player.frames()!=0)throw std::runtime_error("remote mode decoded locally");usleep(5000);
        };
        auto until=[&](const std::function<bool()>&ready,int seconds=12){auto end=Clock::now()+std::chrono::seconds(seconds);while(!ready()){poll();if(Clock::now()>=end)throw std::runtime_error("phase timeout");}};
        auto settle=[&](unsigned ms){auto end=screen::tick()+ms;while(screen::tick()<end)poll();};
        until([&]{return player.loaded&&(!both||player.frames()>2)&&remote.value("owner",std::string())=="bilibili"&&remote.value("state",std::string())=="playing"&&remote.value("position",0.0)>=1;},20);
        content_id=remote.at("content_id");if(both&&!silent_child())throw std::runtime_error("local audio not null");
        double position=remote.value("position",0.0);auto frames=player.frames();until([&]{return remote.value("position",0.0)>=position+1&&(!both||player.frames()>frames+1);},6);
        std::cout<<"PASS "<<(both?"both local frames advance; local audio null":"remote local frames zero")<<"; receiver time advances\n"<<std::flush;
        stage="pause";player.pause();until([&]{return player.paused&&remote.value("state",std::string())=="paused";},6);settle(350);frames=player.frames();position=remote.value("position",0.0);settle(1100);
        if(player.frames()!=frames||std::abs(remote.value("position",0.0)-position)>1.1)throw std::runtime_error("pause not stable");
        std::cout<<"PASS pause holds local frames and receiver time\n"<<std::flush;
        stage="paused seek";double target=std::clamp(source.duration/3.0,3.0,std::min(15.0,double(source.duration-4)));player.seek(target);
        until([&]{return remote.value("state",std::string())=="paused"&&std::abs(remote.value("position",-100.0)-target)<1.6&&(!both||std::abs(player.position-target)<1.6);},8);
        std::cout<<"PASS seek while paused\n"<<std::flush;
        stage="resume";frames=player.frames();player.pause();until([&]{return remote.value("state",std::string())=="playing"&&remote.value("position",0.0)>=target+1&&(!both||player.frames()>frames+1);},8);
        std::cout<<"PASS resume advances receiver and active local decoder\n"<<std::flush;
        stage="stop";player.stop();auto stop_end=Clock::now()+std::chrono::seconds(6);
        while(true){deadline.check();auto state=casting::status();if(state.value("owner",std::string()).empty()&&state.value("state",std::string())=="idle")break;if(Clock::now()>=stop_end)throw std::runtime_error("stop timeout");usleep(100000);}
        std::cout<<"PASS stop releases owned remote playback and relay\n";return 0;
    }catch(...){std::cerr<<"FAIL "<<stage<<" (probe stopped; URL, title and credentials omitted)\n";return 1;}
}
