#include "audio_client.h"
#include "output_mode.hpp"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static void until(const std::function<bool()>& test){for(int n=0;n<100;n++){if(test())return;usleep(30000);}assert(false&&"audio state timeout");}
static c1_audio_request track(int owner,bool bg,const char*url="http://fixture.invalid/long"){
    c1_audio_request r{};r.owner=owner;r.action=C1_AUDIO_PLAY;r.kind=C1_AUDIO_URL;r.background=bg;snprintf(r.url,sizeof r.url,"%s",url);strcpy(r.title,"A test track");return r;
}
int main(int argc,char**argv){
    assert(argc==3);namespace fs=std::filesystem;fs::path root=argv[1],tmp=argv[2];fs::create_directories(tmp/"data");
    setenv("C1_APPS_ROOT",root.c_str(),1);setenv("C1_APPS_DATA",(tmp/"data").c_str(),1);setenv("C1_AUDIO_PLAYER",(root/"fake-player.py").c_str(),1);
    c1_audio_status s{};assert(c1_audio_get(&s)==-1);assert(!fs::exists(tmp/"data/audio/service.sock"));
    assert(c1_audio_background_preference(C1_AUDIO_AIRTUNE,-1)==0);assert(c1_audio_background_preference(C1_AUDIO_AIRTUNE,1)==1);assert(c1_audio_background_preference(C1_AUDIO_AIRTUNE,-1)==1);
    struct stat st{};assert(stat((tmp/"data/airtune/background-playback").c_str(),&st)==0&&(st.st_mode&0777)==0600);
    auto r=track(C1_AUDIO_AIRTUNE,false);assert(c1_audio_call(&r,&s,1)==0);
    until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_PLAYING;});
    assert(c1_audio_command(C1_AUDIO_STREAMPLAYER,C1_AUDIO_STOP,0,nullptr)==-1);assert(c1_audio_get(&s)==0&&s.state==C1_AUDIO_PLAYING);
    assert(c1_audio_command(C1_AUDIO_AIRTUNE,C1_AUDIO_PAUSE,0,&s)==0&&s.state==C1_AUDIO_PAUSED);
    assert(c1_audio_command(C1_AUDIO_AIRTUNE,C1_AUDIO_PAUSE,0,&s)==0&&s.state==C1_AUDIO_PLAYING);
    assert(c1_audio_command(C1_AUDIO_AIRTUNE,C1_AUDIO_DETACH,0,&s)==0&&s.state==C1_AUDIO_IDLE);
    // Background ownership survives the process that started it, then a new UI reattaches.
    pid_t ui=fork();if(!ui){auto p=track(C1_AUDIO_AIRTUNE,true);_exit(c1_audio_call(&p,nullptr,1)==0?0:1);}int status=0;waitpid(ui,&status,0);assert(WIFEXITED(status)&&!WEXITSTATUS(status));
    until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_PLAYING&&s.background==1;});
    assert(c1_audio_command(C1_AUDIO_AIRTUNE,C1_AUDIO_BACKGROUND,0,&s)==0&&s.background==0);
    assert(c1_audio_command(C1_AUDIO_AIRTUNE,C1_AUDIO_DETACH,0,&s)==0&&s.state==C1_AUDIO_IDLE);
    // Non-background child UI death stops playback automatically, without DETACH.
    ui=fork();if(!ui){auto p=track(C1_AUDIO_AIRTUNE,false);if(c1_audio_call(&p,nullptr,1))_exit(1);usleep(150000);_exit(0);}waitpid(ui,&status,0);assert(!status);
    until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_IDLE;});
    // One owner, next/previous and real end-of-file queue advancement.
    r=track(C1_AUDIO_STREAMPLAYER,true,"http://fixture.invalid/short");assert(c1_audio_call(&r,&s,1)==0);
    auto add=track(C1_AUDIO_STREAMPLAYER,true);add.action=C1_AUDIO_APPEND;strcpy(add.title,"Second");assert(c1_audio_call(&add,&s,0)==0&&s.count==2);
    until([&]{return c1_audio_get(&s)==0&&s.index==1&&s.state==C1_AUDIO_PLAYING;});assert(!strcmp(s.title,"Second"));
    assert(c1_audio_command(C1_AUDIO_STREAMPLAYER,C1_AUDIO_PREVIOUS,0,&s)==0&&s.index==0);
    r=track(C1_AUDIO_AIRTUNE,true);assert(c1_audio_call(&r,&s,1)==0&&s.owner==C1_AUDIO_AIRTUNE&&s.count==1);
    r=track(C1_AUDIO_AIRTUNE,true,"file:///etc/passwd");assert(c1_audio_call(&r,&s,0)==-1);assert(c1_audio_get(&s)==0&&s.owner==C1_AUDIO_AIRTUNE);
    r=track(C1_AUDIO_AIRTUNE,true,"http://fixture.invalid/fail");assert(c1_audio_call(&r,&s,1)==0);until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_ERROR;});assert(*s.error);
    // A selected receiver must not override Local. Remote/Both use one remote
    // audible endpoint, and losing that endpoint must never start a local one.
    assert(!fs::exists(root/"cast-loads"));
    auto starts=[&]{std::ifstream f(root/"player-starts");return std::string(std::istreambuf_iterator<char>(f),{});};
    for(auto output:{casting::OutputMode::Remote,casting::OutputMode::Both}){
        assert(casting::set_output_mode("airtune",output));auto local_starts=starts();
        r=track(C1_AUDIO_AIRTUNE,true);assert(c1_audio_call(&r,&s,1)==0);
        until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_PLAYING;});assert(starts()==local_starts);
        assert(c1_audio_command(C1_AUDIO_AIRTUNE,C1_AUDIO_PAUSE,0,&s)==0);
        until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_PAUSED;});
        assert(c1_audio_command(C1_AUDIO_AIRTUNE,C1_AUDIO_SEEK,10,&s)==0);
        until([&]{return c1_audio_get(&s)==0&&s.position_ms==10000;});
        assert(c1_audio_command(C1_AUDIO_AIRTUNE,C1_AUDIO_PAUSE,0,&s)==0);
        until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_PLAYING;});
        std::ofstream(root/"disconnect")<<"1";
        until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_ERROR;});assert(starts()==local_starts&&*s.error);
        assert(c1_audio_call(&r,&s,1)==-1&&s.state==C1_AUDIO_ERROR);assert(starts()==local_starts);
        fs::remove(root/"disconnect");
    }
    assert(casting::set_output_mode("airtune",casting::OutputMode::Local));
    r=track(C1_AUDIO_AIRTUNE,true);assert(c1_audio_call(&r,&s,1)==0);
    until([&]{return c1_audio_get(&s)==0&&s.state==C1_AUDIO_PLAYING;});
    assert(c1_audio_command(0,C1_AUDIO_SHUTDOWN,0,nullptr)==0);until([&]{return !fs::exists(tmp/"data/audio/service.sock");});
    puts("PASS audio service: lazy start, private settings, play/pause, ownership/preemption, UI death, background/reattach, queue, failures and shutdown");
}
