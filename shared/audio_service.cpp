// One audio owner for the three lightweight players. No display/input handles.
#include "audio_client.h"
#include "cast.hpp"
#include "output_mode.hpp"
#include "power_lock.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <vector>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t quitting=0;
void stop(int){quitting=1;}
int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
const char *root(){const char *p=getenv("C1_APPS_ROOT");return p&&*p?p:"/storage/apps/current";}
std::string process_identity(pid_t pid){
    if(pid<=1)return {};
    char path[64],line[1024];snprintf(path,sizeof path,"/proc/%ld/stat",long(pid));FILE *f=fopen(path,"r");
    if(!f)return {};bool ok=fgets(line,sizeof line,f);fclose(f);if(!ok)return {};
    char *end=strrchr(line,')');if(!end||end[2]=='Z')return {};
    char *save=nullptr,*part=strtok_r(end+2," ",&save);
    for(int field=3;part&&field<22;field++)part=strtok_r(nullptr," ",&save);
    return part?part:"";
}
template<size_t N> void text(char(&out)[N],const std::string&s){
    size_t n=std::min(N-1,s.size());while(n&&n<s.size()&&(static_cast<unsigned char>(s[n])&0xc0)==0x80)--n;
    memcpy(out,s.data(),n);out[n]=0;
}
struct Track {int kind=0,song=0;int64_t duration=0;std::string url,title;};
struct Player {
    bool remote=false;
    uint64_t cast_content=0;
    int64_t last_cast=0;
    std::vector<Track> queue;
    c1_audio_status status{};
    pid_t child=-1,ui=-1;
    int input=-1,output=-1,power=-1;
    int64_t started=0,clock_at=0,last_query=0,last_power=0,last_activity=now_ms();
    std::string buffer,ui_identity;
    Player(){status.magic=C1_AUDIO_MAGIC;status.song=-1;}
    ~Player(){stop_child();release_power();}
    bool active()const{return child>0||remote;}
    void command(const std::string &s){if(input>=0){ssize_t n=write(input,s.data(),s.size());(void)n;}}
    void release_power(){if(power>=0){powerlock_command_timeout(power,"susunlock",100);close(power);power=-1;}}
    void update_power(int64_t now){
        if(!active()||status.state==C1_AUDIO_PAUSED){release_power();return;}
        if(now-last_power<5000)return;last_power=now;
        if(power<0){
            power=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
            sockaddr_un addr{};addr.sun_family=AF_UNIX;strcpy(addr.sun_path,"/dev/socket/PowerLock");
            if(power>=0&&connect(power,reinterpret_cast<sockaddr*>(&addr),sizeof addr)){close(power);power=-1;}
        }
        if(power>=0&&powerlock_command_timeout(power,"suslock",100)){close(power);power=-1;}
    }
    void stop_child(){
        if(remote){casting::detach("audio");remote=false;cast_content=0;}
        if(child>0){
            command("quit\n");
            for(int i=0;i<8;i++){if(waitpid(child,nullptr,WNOHANG)==child){child=-1;break;}usleep(10000);}
            if(child>0){kill(-child,SIGTERM);for(int i=0;i<12;i++){if(waitpid(child,nullptr,WNOHANG)==child){child=-1;break;}usleep(10000);}}
            if(child>0){kill(-child,SIGKILL);while(waitpid(child,nullptr,0)<0&&errno==EINTR){}child=-1;}
        }
        if(input>=0)close(input);if(output>=0)close(output);input=output=-1;buffer.clear();release_power();
    }
    void finish(){stop_child();status.state=C1_AUDIO_IDLE;status.position_ms=0;last_activity=now_ms();}
    void error(const std::string &message){stop_child();status.state=C1_AUDIO_ERROR;text(status.error,message);last_activity=now_ms();}
    void select(int index){
        stop_child();if(index<0||index>=int(queue.size())){status.state=C1_AUDIO_IDLE;return;}
        status.index=index;status.count=queue.size();status.position_ms=0;status.duration_ms=queue[index].duration;
        status.song=queue[index].kind==C1_AUDIO_SONG?queue[index].song:-1;
        text(status.title,queue[index].title);text(status.url,queue[index].url);status.error[0]=0;
        const auto output_mode=casting::output_mode(status.owner==C1_AUDIO_AIRTUNE?"airtune":"streamplayer");
        // Both retains the app's cover/transport UI locally, but has a single
        // audible endpoint. Never unexpectedly start the speaker after failure.
        if(queue[index].kind==C1_AUDIO_URL&&output_mode!=casting::OutputMode::Local){
            if(!casting::selected()){error("未连接接收器，请连接投屏或在设置中选择仅本机");return;}
            const auto&t=queue[index];auto result=casting::load("audio",t.url,t.title,casting::audio_mime(t.url),t.duration==0);
            if(!result.value("ok",false)){error(result.value("error",std::string("投屏发送失败")));return;}
            cast_content=result.value("accepted_id",uint64_t(0));remote=true;status.state=C1_AUDIO_CONNECTING;started=clock_at=now_ms();last_cast=0;return;
        }
        int in[2],out[2];if(pipe2(in,O_CLOEXEC)){error("Cannot create audio input pipe");return;}
        if(pipe2(out,O_CLOEXEC)){close(in[0]);close(in[1]);error("Cannot create audio status pipe");return;}
        const auto &track=queue[index];
        std::string executable,number=std::to_string(track.song);std::vector<std::string> args;
        if(track.kind==C1_AUDIO_SONG){executable=track.url.empty()?std::string(root())+"/piano/c1max-piano":track.url;args={executable,"--audio-only","--song",number};}
        else {const char *override=getenv("C1_AUDIO_PLAYER");executable=override&&*override?override:"/usr/bin/mplayer";
            args={executable,"-noconfig","all","-slave","-quiet","-identify","-noconsolecontrols","-nolirc","-nojoystick","-nomouseinput","-novideo","-vo","null","-ao","media","-cache","128","-cache-min","5",track.url};}
        std::vector<char*> av;for(auto &a:args)av.push_back(a.data());av.push_back(nullptr);
        pid_t parent=getpid();child=fork();
        if(!child){setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGTERM);if(getppid()!=parent)_exit(1);
            dup2(in[0],0);dup2(out[1],1);dup2(out[1],2);close(in[0]);close(in[1]);close(out[0]);close(out[1]);execv(executable.c_str(),av.data());_exit(127);}
        close(in[0]);close(out[1]);
        if(child<0){close(in[1]);close(out[0]);error("Cannot start audio player");return;}
        setpgid(child,child);input=in[1];output=out[0];fcntl(input,F_SETFL,O_NONBLOCK);fcntl(output,F_SETFL,O_NONBLOCK);
        status.state=C1_AUDIO_CONNECTING;started=clock_at=now_ms();last_query=0;last_power=0;
    }
    void line(const std::string &s){
        if(s.find("Starting playback")!=std::string::npos||s.rfind("AO:",0)==0||s=="C1_PIANO_READY"){
            if(status.state==C1_AUDIO_CONNECTING){status.state=C1_AUDIO_PLAYING;clock_at=now_ms();}
        }
        const char *position=nullptr;
        if(s.rfind("ANS_TIME_POSITION=",0)==0)position=s.c_str()+18;
        else if(s.rfind("C1_POSITION=",0)==0)position=s.c_str()+12;
        if(position){char *end=nullptr;double value=strtod(position,&end);
            if(end!=position&&std::isfinite(value)&&value>=0&&value<7*86400){status.position_ms=int64_t(value*1000);clock_at=now_ms();}}
        if(s.rfind("ID_LENGTH=",0)==0&&!status.duration_ms){char *end=nullptr;double length=strtod(s.c_str()+10,&end);if(end!=s.c_str()+10&&std::isfinite(length)&&length>0&&length<7*86400)status.duration_ms=int64_t(length*1000);}
    }
    void poll_player(){
        auto now=now_ms();
        if(active()&&!status.background&&(ui<=1||process_identity(ui)!=ui_identity)){finish();return;}
        if(remote){
            if(now-last_cast<500){update_power(now);return;}last_cast=now;auto s=casting::status();
            if(!s.value("connected",false)){error("电视已断开，请重新选择本机播放");return;}
            if(s.value("content_id",uint64_t(0))!=cast_content){if(now-started>15000)error(s.value("error",std::string()).empty()?"投屏内容已切换或加载失败":s.value("error",std::string()));return;}
            auto state=s.value("state",std::string());if(state=="error"){error(s.value("error",std::string("电视播放失败")));return;}
            if(state=="playing")status.state=C1_AUDIO_PLAYING;else if(state=="paused")status.state=C1_AUDIO_PAUSED;
            else if(state=="idle"&&status.state!=C1_AUDIO_CONNECTING){if(status.index+1<int(queue.size()))select(status.index+1);else finish();return;}
            status.position_ms=int64_t(std::max(0.0,s.value("position",0.0))*1000);auto duration=s.value("duration",0.0);if(duration>0)status.duration_ms=int64_t(duration*1000);
            if(status.state==C1_AUDIO_CONNECTING&&now-started>45000){error("电视连接音频超时");return;}update_power(now);return;
        }
        if(output>=0){char b[2048];ssize_t n;
            while((n=read(output,b,sizeof b))>0){for(ssize_t i=0;i<n;i++)if(b[i]=='\r')b[i]='\n';buffer.append(b,n);
                size_t cut;while((cut=buffer.find('\n'))!=std::string::npos){line(buffer.substr(0,cut));buffer.erase(0,cut+1);}if(buffer.size()>8192)buffer.clear();}
        }
        if(active()&&status.state==C1_AUDIO_PLAYING){status.position_ms+=std::max<int64_t>(0,now-clock_at);clock_at=now;}
        if(active()&&now-last_query>=500){command("pausing_keep_force get_time_pos\n");last_query=now;}
        if(active()&&status.state==C1_AUDIO_CONNECTING&&now-started>45000){error("Audio stream connection timed out");return;}
        if(active()){
            int value=0;if(waitpid(child,&value,WNOHANG)==child){child=-1;
                if(WIFEXITED(value)&&WEXITSTATUS(value)==0&&status.state!=C1_AUDIO_CONNECTING){if(status.index+1<int(queue.size()))select(status.index+1);else finish();}
                else error("Audio stream could not be played");
            }
        }
        update_power(now);
    }
    bool valid_track(const c1_audio_request &r)const{
        if(!memchr(r.url,0,sizeof r.url)||!memchr(r.title,0,sizeof r.title)||r.duration_ms<0||r.duration_ms>int64_t(7)*86400000)return false;
        if(r.kind==C1_AUDIO_SONG)return r.owner==C1_AUDIO_PIANO&&r.index>=0&&r.index<32&&r.url[0]=='/'&&std::string(r.url).find("/piano/c1max-piano")!=std::string::npos;
        if(r.kind!=C1_AUDIO_URL||r.owner==C1_AUDIO_PIANO)return false;
        std::string url=r.url;if(url.rfind("http://",0)&&url.rfind("https://",0))return false;
        return url.size()>8&&url.find_first_of("\r\n\t ")==std::string::npos;
    }
    int request(const c1_audio_request &r,pid_t caller){
        last_activity=now_ms();
        if(r.magic!=C1_AUDIO_MAGIC||r.owner<0||r.owner>3)return -1;
        if(r.action==C1_AUDIO_STATUS)return 0;
        if(r.action==C1_AUDIO_SHUTDOWN){finish();quitting=1;return 0;}
        if(r.action==C1_AUDIO_PLAY){
            if(!r.owner||!valid_track(r))return -1;
            finish();queue.clear();queue.push_back({r.kind,r.index,r.duration_ms,r.url,r.title});
            ui=caller;ui_identity=process_identity(ui);status.owner=r.owner;status.background=!!r.background;select(0);return status.state==C1_AUDIO_ERROR?-1:0;
        }
        if(r.owner!=status.owner&&!(r.action==C1_AUDIO_STOP&&r.owner==0))return -1;
        if(r.action==C1_AUDIO_STOP){finish();return 0;}
        if(r.action==C1_AUDIO_APPEND){if(queue.size()>=C1_AUDIO_QUEUE_MAX||!valid_track(r))return -1;queue.push_back({r.kind,r.index,r.duration_ms,r.url,r.title});status.count=queue.size();return 0;}
        if(r.action==C1_AUDIO_BACKGROUND){status.background=!!r.value;ui=caller;ui_identity=process_identity(ui);return 0;}
        if(r.action==C1_AUDIO_DETACH){if(!status.background)finish();ui=-1;ui_identity.clear();return 0;}
        if(r.action==C1_AUDIO_NEXT||r.action==C1_AUDIO_PREVIOUS){if(queue.empty())return -1;ui=caller;ui_identity=process_identity(ui);select((status.index+int(queue.size())+(r.action==C1_AUDIO_NEXT?1:-1))%queue.size());return 0;}
        if(!active())return -1;
        if(r.action==C1_AUDIO_PAUSE){if(status.state==C1_AUDIO_CONNECTING)return -1;if(remote){auto s=casting::command("audio",status.state==C1_AUDIO_PAUSED?"play":"pause");return s.value("ok",false)?0:-1;}command("pause\n");status.state=status.state==C1_AUDIO_PAUSED?C1_AUDIO_PLAYING:C1_AUDIO_PAUSED;clock_at=now_ms();return 0;}
        if(r.action==C1_AUDIO_SEEK){int seconds=std::clamp(r.value,-3600,3600);if(remote){auto s=casting::command("audio","seek",std::max(0.0,status.position_ms/1000.0+seconds));return s.value("ok",false)?0:-1;}command("pausing_keep_force seek "+std::to_string(seconds)+" 0\n");status.position_ms=std::max<int64_t>(0,status.position_ms+seconds*1000);clock_at=now_ms();return 0;}
        return -1;
    }
};
}

int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--stop"))return c1_audio_command(0,C1_AUDIO_SHUTDOWN,0,nullptr)==0?0:1;
    if(argc!=2||strcmp(argv[1],"--daemon"))return 2;
    signal(SIGTERM,stop);signal(SIGINT,stop);signal(SIGHUP,stop);signal(SIGPIPE,SIG_IGN);umask(077);
    sockaddr_un addr{};addr.sun_family=AF_UNIX;if(c1_audio_socket_path(addr.sun_path,sizeof addr.sun_path))return 1;
    std::string path=addr.sun_path,dir=path.substr(0,path.rfind('/'));
    if(mkdir(dir.c_str(),0700)&&errno!=EEXIST)return 1;
    int lock=open((dir+"/service.lock").c_str(),O_RDWR|O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600);if(lock<0)return 1;
    if(flock(lock,LOCK_EX|LOCK_NB)){close(lock);return 0;}
    int server=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0);if(server<0){close(lock);return 1;}
    unlink(path.c_str());if(bind(server,reinterpret_cast<sockaddr*>(&addr),sizeof addr)||chmod(path.c_str(),0600)||listen(server,8)){close(server);close(lock);return 1;}
    {
        Player player;
        while(!quitting){
            player.poll_player();if(!player.active()&&now_ms()-player.last_activity>30000)break;
            pollfd p{server,POLLIN,0};int count=poll(&p,1,20);if(count<0){if(errno==EINTR)continue;break;}
            if(count>0&&(p.revents&POLLIN)){
                int client=accept4(server,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK);if(client<0)continue;
                ucred cred{};socklen_t size=sizeof cred;
                if(getsockopt(client,SOL_SOCKET,SO_PEERCRED,&cred,&size)||cred.uid!=geteuid()){close(client);continue;}
                pollfd wait{client,POLLIN,0};c1_audio_request request{};
                ssize_t n=poll(&wait,1,100)>0?recv(client,&request,sizeof request,MSG_TRUNC):-1;
                int result=n==(ssize_t)sizeof request?player.request(request,cred.pid):-1;
                auto response=player.status;response.result=result;send(client,&response,sizeof response,MSG_NOSIGNAL);close(client);
            }
        }
    }
    close(server);unlink(path.c_str());close(lock);return 0;
}
