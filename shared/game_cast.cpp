#include "game_cast.hpp"
#include "game_cast_c.h"
#include "cast.hpp"
#include "output_mode.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
namespace game_cast {
namespace {
using Clock=std::chrono::steady_clock;
constexpr unsigned Width=320,Height=240,Fps=20,AudioBytes=44100;
uint32_t milliseconds(){return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());}
struct State {
    std::atomic<bool> enabled{false},stopping{false},playing{false};
    std::atomic<uint32_t> next_frame{0};
    std::atomic<int> socket{-1};
    casting::OutputMode mode=casting::OutputMode::Local;
    std::string app,title,notice;
    std::mutex mutex;
    std::condition_variable ready;
    std::thread worker;
    std::array<uint8_t,Width*Height*2> frame{};
    std::array<uint8_t,AudioBytes> pcm{};
    size_t audio_begin=0,audio_count=0;
    unsigned audio_phase=0,audio_rate=0;
    bool frame_ready=false;
    void say(const std::string &s){std::lock_guard<std::mutex> lock(mutex);notice=s;}
    ~State(){stopping=true;ready.notify_all();if(worker.joinable())worker.join();}
} state;

bool wait_socket(int fd,short events,int ms){pollfd p{fd,events,0};int n;do{n=poll(&p,1,ms);}while(n<0&&errno==EINTR&&!state.stopping);return n>0&&(p.revents&events)&&!(p.revents&(POLLERR|POLLNVAL));}
bool send_all(int fd,const void *ptr,size_t size){
    auto *p=static_cast<const uint8_t*>(ptr);const size_t total=size;const auto until=Clock::now()+std::chrono::milliseconds(300);
    while(size&&!state.stopping){ssize_t n=send(fd,p,size,MSG_NOSIGNAL);if(n>0){p+=n;size-=size_t(n);continue;}
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)&&Clock::now()<until){wait_socket(fd,POLLOUT,25);continue;}
        std::fprintf(stderr,"Game bridge send failed: errno=%d sent=%zu/%zu timeout=%d\n",n<0?errno:0,total-size,total,Clock::now()>=until);return false;}
    return !size;
}
bool packet(int fd,char type,const void *data,size_t size){uint8_t h[8]={uint8_t(type),0,0,0,uint8_t(size>>24),uint8_t(size>>16),uint8_t(size>>8),uint8_t(size)};return send_all(fd,h,sizeof h)&&send_all(fd,data,size);}
int connect_bridge(const std::string &host,unsigned port){
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(uint16_t(port));if(inet_pton(AF_INET,host.c_str(),&addr.sin_addr)!=1)return -1;
    int fd=::socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;
#ifdef SO_NOSIGPIPE
    int yes=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes);
#endif
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL)|O_NONBLOCK);int limit=262144;setsockopt(fd,SOL_SOCKET,SO_SNDBUF,&limit,sizeof limit);
    if(connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof addr)<0){
        int error=0;socklen_t length=sizeof error;
        if(errno!=EINPROGRESS||!wait_socket(fd,POLLOUT,1000)||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)||error){close(fd);return -1;}}
    return fd;
}
// Control replies are newline JSON, capped across fragmented TCP reads.
bool replies(int fd,std::string &buffer,std::vector<casting::Json> &out){
    char data[4096];for(unsigned i=0;i<4;i++){
        ssize_t n=recv(fd,data,sizeof data,0);if(n==0)return !out.empty();
        if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK)break;if(errno==EINTR)continue;return false;}
        buffer.append(data,size_t(n));if(buffer.size()>16384)return false;
        size_t newline;while((newline=buffer.find('\n'))!=std::string::npos){auto j=casting::Json::parse(buffer.begin(),buffer.begin()+newline,nullptr,false);buffer.erase(0,newline+1);if(!j.is_object())return false;out.push_back(std::move(j));if(out.size()>8)return false;}}
    return true;
}
void worker(){
    int fd=-1;bool owned=false;std::string error="Cast disconnected; local restored";
    auto fail=[&](const char *message){error=message;};
    try {
        const char *base=getenv("C1_APPS_DATA");std::string path=std::string(base?base:"/storage/apps/data")+"/cast/game-bridge.json";
        struct stat st{};if(stat(path.c_str(),&st)||!S_ISREG(st.st_mode)||st.st_size>4096||(st.st_mode&0077))throw std::runtime_error("Pair this Mac first; using local");
        std::ifstream f(path);auto config=casting::Json::parse(f,nullptr,false);
        if(!config.is_object())throw std::runtime_error("Invalid Mac pairing; using local");
        std::string host=config.value("host",""),token=config.value("token","");int port=config.value("port",28766);
        if(token.size()!=64||token.find_first_not_of("0123456789abcdef")!=std::string::npos||port<1024||port>65535)throw std::runtime_error("Invalid Mac pairing; using local");
        auto status=casting::status();
        if(!status.value("connected",false)||status.value("device",casting::Json::object()).value("host","")!=host)throw std::runtime_error("Select the paired Mac in Settings");
        fd=connect_bridge(host,unsigned(port));if(fd<0)throw std::runtime_error("Mac bridge offline; using local");state.socket=fd;
        auto hello=casting::Json{{"protocol","c1max-game-v1"},{"token",token},{"app",state.app},{"title",state.title.substr(0,256)},{"width",Width},{"height",Height},{"fps",Fps},{"format","rgb565le"},{"sample_rate",44100},{"channels",2}}.dump()+"\n";
        if(!send_all(fd,hello.data(),hello.size()))throw std::runtime_error("Mac handshake failed; using local");
        std::string buffer,url;bool accepted=false,loaded=false,confirmed=false;uint64_t content_id=0;auto began=Clock::now(),last_reply=began,last_status=began,last_send=began;
        std::array<uint8_t,Width*Height*2> frame{};std::array<uint8_t,16384> audio{};
        while(!state.stopping){
            std::vector<casting::Json> responses;if(!replies(fd,buffer,responses)){fail("Mac connection closed; local restored");break;}
            for(auto &r:responses){
                if(!r.value("ok",false))throw std::runtime_error("Mac bridge rejected stream; local");
                last_reply=Clock::now();
                if(r.value("protocol","")=="c1max-game-v1")accepted=true;
                if(r.value("ready",false)&&url.empty()){
                    auto candidate=r.value("url","");std::string prefix="http://"+host+":";
                    // Only accept a private media capability on this exact Mac.
                    if(candidate.compare(0,prefix.size(),prefix)!=0||candidate.find("/session/")==std::string::npos||candidate.find_first_of("\r\n@")!=std::string::npos)throw std::runtime_error("Invalid Mac media URL; using local");
                    url=candidate;
                }
            }
            auto now=Clock::now();
            if(!accepted&&now-began>std::chrono::seconds(3)){fail("Mac handshake timed out; local");break;}
            if(now-last_reply>std::chrono::seconds(4)){fail("Mac bridge lost; local restored");break;}
            if(accepted){
                bool video=false;size_t bytes=0;
                {std::unique_lock<std::mutex> lock(state.mutex);if(!state.frame_ready&&!state.audio_count)state.ready.wait_for(lock,std::chrono::milliseconds(10));
                    if(state.frame_ready){frame=state.frame;video=true;state.frame_ready=false;}
                    bytes=std::min(audio.size(),state.audio_count);bytes-=bytes%4;
                    for(size_t i=0;i<bytes;i++)audio[i]=state.pcm[(state.audio_begin+i)%AudioBytes];
                    state.audio_begin=(state.audio_begin+bytes)%AudioBytes;state.audio_count-=bytes;}
                if(bytes&&!packet(fd,'A',audio.data(),bytes)){fail("Mac audio upload stalled; local");break;}
                if(video&&!packet(fd,'V',frame.data(),frame.size())){fail("Mac video upload stalled; local");break;}
                if(bytes||video)last_send=now;
                // Pausing in an emulator's menu stops both callbacks. Keep
                // ownership alive without queuing stale media or touching its
                // simulation clock; the Mac repeats video and fills silence.
                if(now-last_send>=std::chrono::milliseconds(500)){
                    if(!packet(fd,'P',nullptr,0)){fail("Mac heartbeat failed; local");break;}last_send=now;
                }
            }else std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if(!url.empty()&&!loaded){
                auto reply=casting::load(state.app,url,state.title,"application/x-mpegURL",true);
                if(!reply.value("ok",false))throw std::runtime_error("Receiver refused game; using local");
                content_id=reply.value("accepted_id",uint64_t(0));
                loaded=owned=true;state.say("TV buffering; local stays on");
            }
            if(now-last_status>=std::chrono::milliseconds(700)){
                last_status=now;auto s=casting::status();
                if(!s.value("connected",false)||s.value("device",casting::Json::object()).value("host","")!=host){fail("Receiver disconnected; local restored");break;}
                if(loaded){
                    bool current=s.value("owner","")==state.app&&s.value("content_id",uint64_t(0))==content_id;
                    // load() is queued IPC. An older owner can remain visible
                    // while the daemon completes the receiver's SOAP request.
                    if(!current){if(confirmed){fail("Receiver changed stream; local");break;}continue;}
                    confirmed=true;
                    if(!s.value("error","").empty()){fail("Receiver playback failed; local");break;}
                    if(state.playing&&s.value("state","")=="idle"){fail("Receiver stopped; local restored");break;}
                    if(s.value("state","")=="playing")state.playing=true;
                    // Once accepted by the sink, temporary buffering/pause
                    // must not flash the local image or produce double audio.
                    state.say(state.playing?(state.mode==casting::OutputMode::Remote?"Playing on Mac / controls here":""):"TV buffering; local stays on");
                }
            }
            if(!state.playing&&now-began>std::chrono::seconds(30)){fail("TV start timed out; using local");break;}
        }
    }catch(const std::exception &e){error=e.what();}
    state.enabled=false;
    if(owned)casting::detach(state.app);
    state.socket=-1;if(fd>=0)close(fd);
    // Explicit stop is kept muted until audio teardown; error fallback resumes
    // only after asking the sink to stop and closing its media producer.
    state.playing=false;
    if(!state.stopping){state.say(error);std::fprintf(stderr,"Game cast: %s\n",error.c_str());}
}

template<class Pixel> void frame(const Pixel *pixels,unsigned w,unsigned h,size_t pitch,unsigned an,unsigned ad,int format){
    if(!state.enabled||!pixels||!w||!h||w>2048||h>2048||pitch<size_t(w)*sizeof(Pixel)||!an||!ad)return;
    std::unique_lock<std::mutex> lock(state.mutex,std::try_to_lock);if(!lock.owns_lock())return;
    unsigned vw=Width,vh=Height;
    if(uint64_t(an)*Height>uint64_t(ad)*Width)vh=std::max(1u,unsigned(uint64_t(Width)*ad/an));
    else vw=std::max(1u,unsigned(uint64_t(Height)*an/ad));
    unsigned left=(Width-vw)/2,top=(Height-vh)/2;
    unsigned xx[Width];for(unsigned x=0;x<vw;x++)xx[x]=x*w/vw;
    std::fill(state.frame.begin(),state.frame.end(),0);
    for(unsigned y=0;y<vh;y++){
        auto *row=reinterpret_cast<const Pixel*>(reinterpret_cast<const uint8_t*>(pixels)+size_t(y*h/vh)*pitch);
        for(unsigned x=0;x<vw;x++){
            uint32_t p=row[xx[x]];uint16_t v;
            if(format==0)v=uint16_t(((p&0x7c00)<<1)|((p&0x03e0)<<1)|((p&0x0200)>>4)|(p&0x001f));
            else if(format==1)v=uint16_t(p);
            else v=uint16_t(((p>>8)&0xf800)|((p>>5)&0x07e0)|((p>>3)&0x001f));
            size_t i=(size_t(top+y)*Width+left+x)*2;state.frame[i]=uint8_t(v);state.frame[i+1]=uint8_t(v>>8);
        }
    }
    state.frame_ready=true;lock.unlock();state.ready.notify_one();
}
}
void start(const std::string &app,const std::string &title){
    stop();state.app=app;state.title=title;state.mode=casting::output_mode(app);state.stopping=false;state.playing=false;
    if(state.mode==casting::OutputMode::Local){state.say("");return;}
    {std::lock_guard<std::mutex> lock(state.mutex);state.frame_ready=false;state.audio_begin=state.audio_count=state.audio_phase=state.audio_rate=0;}
    state.enabled=true;state.next_frame=0;state.say("Connecting Mac; local stays on");state.worker=std::thread(worker);
}
void stop(){state.stopping=true;state.enabled=false;state.playing=false;state.ready.notify_all();int fd=state.socket.load();if(fd>=0)shutdown(fd,SHUT_RDWR);if(state.worker.joinable())state.worker.join();}
bool video_due(){if(!state.enabled)return false;uint32_t now=milliseconds(),next=state.next_frame.load();if(next&&int32_t(now-next)<0)return false;uint32_t following=next&&int32_t(now-next)<100?next+1000/Fps:now+1000/Fps;return state.next_frame.compare_exchange_strong(next,following);}
void frame_rgb555(const uint16_t *p,unsigned w,unsigned h,size_t stride,unsigned an,unsigned ad){frame(p,w,h,stride,an,ad,0);}
void frame_rgb565(const uint16_t *p,unsigned w,unsigned h,size_t stride,unsigned an,unsigned ad){frame(p,w,h,stride,an,ad,1);}
void frame_xrgb8888(const uint32_t *p,unsigned w,unsigned h,size_t stride,unsigned an,unsigned ad){frame(p,w,h,stride,an,ad,2);}
void audio(const int16_t *data,size_t frames,unsigned channels,unsigned rate){
    if(!state.enabled||!data||!frames||frames>65536||(channels!=1&&channels!=2)||rate<8000||rate>96000)return;
    std::unique_lock<std::mutex> lock(state.mutex,std::try_to_lock);if(!lock.owns_lock())return;
    if(state.audio_rate!=rate){state.audio_rate=rate;state.audio_phase=0;}
    // A rational streaming nearest-neighbour resampler; no allocation and no
    // per-callback rounding drift. Native emulator output is normally 44100 Hz.
    for(size_t i=0;i<frames;i++){
        state.audio_phase+=44100;
        while(state.audio_phase>=rate){state.audio_phase-=rate;
            uint16_t l=uint16_t(data[i*channels]),r=uint16_t(data[i*channels+channels-1]);uint8_t bytes[]={uint8_t(l),uint8_t(l>>8),uint8_t(r),uint8_t(r>>8)};
            if(state.audio_count+4>AudioBytes){state.audio_begin=(state.audio_begin+4)%AudioBytes;state.audio_count-=4;}
            for(auto b:bytes){state.pcm[(state.audio_begin+state.audio_count)%AudioBytes]=b;++state.audio_count;}
        }
    }
    lock.unlock();state.ready.notify_one();
}
bool local_video(){return state.mode!=casting::OutputMode::Remote||!state.playing;}
bool local_audio(){return !state.stopping&&!state.playing;}
std::string message(){std::lock_guard<std::mutex> lock(state.mutex);return state.notice;}
}
extern "C" void c1_game_cast_audio(const int16_t *p,size_t frames,unsigned channels,unsigned rate){game_cast::audio(p,frames,channels,rate);}
extern "C" int c1_game_cast_local_audio(){return game_cast::local_audio()?1:0;}
