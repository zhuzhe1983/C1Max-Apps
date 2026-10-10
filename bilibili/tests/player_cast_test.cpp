// Player lifecycle test with the real FIFO decoder and a disposable fake MPlayer.
// Cast receiver state is injected; this does not claim a real receiver test.
#include "player.hpp"
#include "cast.hpp"
#include "display.hpp"
#include <cassert>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace {
casting::OutputMode requested=casting::OutputMode::Local;
Json state={{"connected",true},{"device",{{"host","127.0.0.1"}}},{"owner",""},{"state","idle"},{"content_id",0}};
Json commands=Json::array();uint64_t serial=0;int cast_calls=0,local_begins=0;uint32_t extra_tick=0;
void advance(){extra_tick+=700;}
void ready(uint64_t id){state["content_id"]=id;state["owner"]="bilibili";state["state"]="playing";state["position"]=5.0;state["duration"]=100;state["volume"]=0.5;state["error"]="";}
}
namespace casting {
OutputMode output_mode(const std::string&){return requested;}
const char*output_mode_label(OutputMode m){return m==OutputMode::Local?"local":m==OutputMode::Remote?"remote":"both";}
Json status(){++cast_calls;return state;}
Json load(const std::string&o,const std::string&url,const std::string&,const std::string&,bool,const std::string&,double){++cast_calls;assert(o=="bilibili"&&url=="http://127.0.0.1/current/video.mp4");return {{"ok",true},{"accepted",true},{"accepted_id",++serial}};}
Json command(const std::string&o,const std::string&a,double v){commands.push_back({{"owner",o},{"action",a},{"value",v}});return {{"ok",true}};}
void detach(const std::string&o){command(o,"stop",0);}
}
namespace bili {
CastRelay::~CastRelay()=default;
std::string CastRelay::start(const std::string&,const std::string&){return "http://127.0.0.1/current/video.mp4";}
void CastRelay::stop(){}
}
namespace screen {
bool playing=false,tap=false;
uint32_t tick(){return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count())+extra_tick;}
bool video_begin(){++local_begins;return true;}
void video_frame(const uint32_t*,int,int,int,int){}
void video_refresh(bool){}
void video_end(){}
}
int main(){signal(SIGPIPE,SIG_IGN);assert(getenv("C1_APPS_DATA"));std::filesystem::create_directories(c1::data()+"/bilibili");bili::Stream stream{"https://fixture.bilivideo.com/video.mp4",100,16};
    auto until=[](bili::Player&p){for(int i=0;i<300&&!p.loaded;i++){p.poll();assert(p.error.empty());usleep(10000);}assert(p.loaded);};
    {
        requested=casting::OutputMode::Local;state["connected"]=false;bili::Player p;p.start(stream);until(p);assert(cast_calls==0&&local_begins==1);auto args=c1::read_file(c1::data()+"/args");assert(args.find("\n-ao\nmedia\n")!=std::string::npos);p.stop();
    }
    {
        requested=casting::OutputMode::Remote;bili::Player p;bool failed=false;try{p.start(stream);}catch(...){failed=true;}assert(failed&&!p.active()&&local_begins==1);
        state["connected"]=true;p.start(stream);auto id=serial;advance();p.poll();assert(!p.loaded&&p.error.empty());ready(id);advance();p.poll();assert(p.loaded&&p.remote_only()&&local_begins==1);p.pause();assert(p.paused&&commands.back()["action"]=="pause");p.seek(22);assert(commands.back()["action"]=="seek"&&commands.back()["value"]==22);p.set_remote_volume(17);assert(commands.back()["action"]=="volume");
        state["connected"]=false;advance();p.poll();assert(p.ended&&!p.active()&&!p.error.empty()&&local_begins==1);
    }
    {
        requested=casting::OutputMode::Both;state["connected"]=true;state["owner"]="other";commands=Json::array();bili::Player p;p.start(stream);auto id=serial;until(p);assert(local_begins==2&&!p.remote_only());auto args=c1::read_file(c1::data()+"/args");assert(args.find("\n-ao\nnull\n")!=std::string::npos);
        // Local is ready first. Defer remote pause/seek until the new content is ready.
        p.pause();p.seek(22);assert(p.paused&&commands.empty());ready(id);advance();p.poll();assert(commands.size()==2&&commands[0]["action"]=="seek"&&commands[0]["value"]==22&&commands[1]["action"]=="pause");
        p.pause();assert(!p.paused&&commands.back()["action"]=="play");p.seek(33);assert(commands.back()["action"]=="seek");
        state["content_id"]=id+1;state["owner"]="successor";advance();p.poll();assert(p.ended&&!p.active()&&!p.error.empty()&&local_begins==2);
    }
    std::cout<<"PASS player: local isolation, no-receiver refusal, remote controls, pending content fencing, disconnect, muted both, deferred pause/seek, cleanup\n";
}
