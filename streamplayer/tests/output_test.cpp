// Exercise the real UI's output state machine without media hardware/network.
#define main streamplayer_application_main
#include "../src/main.cpp"
#undef main
#include <cassert>

namespace casting {
static Json test_status={{"ok",true},{"connected",true},{"state","playing"},{"content_id",7},{"position",10.0},{"volume_available",true},{"volume",.4}};
static std::vector<Json> operations;
Json call(const Json&,bool){return test_status;}
Json status(){return test_status;}
bool selected(){return test_status.value("connected",false);}
Json load(const std::string&o,const std::string&u,const std::string&t,const std::string&m,bool live,const std::string&i,double at){operations.push_back({{"action","load"},{"url",u},{"owner",o},{"position",at}});return {{"ok",true},{"accepted_id",7}};}
Json command(const std::string&o,const std::string&a,double v){operations.push_back({{"owner",o},{"action",a},{"value",v}});return {{"ok",true}};}
void detach(const std::string&o){command(o,"stop",0);}
}
static void poll_now(){tv_poll_at=screen::tick()-701;poll_tv();}
int main(int argc,char**argv){
    assert(argc==3);setenv("C1_APPS_ROOT",argv[1],1);setenv("C1_APPS_DATA",argv[2],1);fs::create_directories(argv[2]);assert(screen::open());
    current.item="fixture";current.source="source";current.session="local-session";current.duration=6000000000;current.options.television=false;
    Playback remote=current;remote.session="tv-session";remote.options.television=true;remote.hls_url="http://fixture.invalid/master.m3u8";
    video_output=casting::OutputMode::Both;assert(casting::set_output_mode("streamplayer",video_output));
    assert(begin_tv(remote,10));assert(tv_casting&&television_dirty&&current.session=="local-session");
    assert(casting::operations.size()==1&&casting::operations[0]["position"]==10.0);
    int pipe[2];assert(!pipe2(pipe,O_CLOEXEC|O_NONBLOCK));player_in=pipe[1];
    screen::playing=true;have_time=true;position=100000000;paused=false;restarting=false;pause_on_start=false;
    const std::string frame=std::string("YUV4MPEG2 W2 H2 F20:1 Ip C420\nFRAME\n")+std::string(6,'\x80');
    assert(video_reader.feed(reinterpret_cast<const uint8_t*>(frame.data()),frame.size(),[](const uint32_t*,int,int,int,int){}));
    toggle_pause();assert(casting::operations.back()["action"]=="pause");assert(!paused); // local follows receiver acknowledgement
    casting::test_status["state"]="paused";poll_now();assert(paused&&!tv_pause_requested);
    char command_bytes[128]{};assert(read(pipe[0],command_bytes,sizeof command_bytes)>0);assert(std::string(command_bytes)=="pause\n");
    toggle_pause();assert(casting::operations.back()["action"]=="play");casting::test_status["state"]="playing";poll_now();assert(!paused);
    tv_seek(10);assert(casting::operations.back()["action"]=="seek"&&casting::operations.back()["value"]==20.0);
    auto count=casting::operations.size();toggle_fit();assert(casting::operations.size()==count&&!requested_options.television); // fit is local only
    // Remote audio is the clock; buffering pauses local display without opening audio.
    casting::test_status["state"]="buffering";poll_now();assert(paused);
    casting::test_status["state"]="playing";casting::test_status["position"]=20.0;tv_sync_at=screen::tick()-10001;poll_now();assert(seek_target==200000000&&local_sync_seek);
    // Disconnect stops both, retains preference and presents an explicit local fallback.
    casting::test_status["connected"]=false;poll_now();assert(!screen::playing&&!tv_casting&&playback_failed&&ended&&view==View::Cast);
    assert(casting::output_mode("streamplayer")==casting::OutputMode::Both);
    assert(retired_sessions.size()==1&&retired_sessions[0].session=="tv-session"&&retired_sessions[0].options.television);
    assert(current.session=="local-session"&&player_in==-1);close(pipe[0]);
    // Pure remote owns the current session; stopping must not retire it twice.
    retired_sessions.clear();current=remote;video_output=casting::OutputMode::Remote;casting::test_status["connected"]=true;
    assert(begin_tv(remote));assert(view==View::Cast&&!screen::playing);stop_tv();assert(retired_sessions.empty());
    lv_obj_clean(lv_screen_active());screen::close();puts("PASS StreamPlayer output state: separate sessions, remote controls, local fit, muted-clock sync, disconnect and explicit local fallback");
}
