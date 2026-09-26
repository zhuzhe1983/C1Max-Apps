#include "player.hpp"
#include "display.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <signal.h>
#include <stdexcept>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
namespace bili {
Player::~Player(){stop();}
bool Player::command(const std::string&s,const char*prefix){if(input_<0)return false;auto text=prefix+s+"\n";if(write(input_,text.data(),text.size())!=(ssize_t)text.size()){error="播放器控制连接已断开";return false;}return true;}
void Player::start(const Stream&s){
    stop();error.clear();ended=loaded=paused=false;position=0;duration=s.duration;reader_=Y4mReader{};lines_.clear();
    bool local=getenv("C1_BILI_QA_LOCAL")&&s.url=="/tmp/c1-bili-direct.mp4";
    if(!local&&!media_url(s.url))throw std::runtime_error("无效的视频源");
    // The stock libavformat HTTPS stream crashes with SIGBUS on byte seeks,
    // including -ss at startup. Its native HTTP transport supports CDN Range
    // seeks. Only media uses HTTP; account/API traffic stays HTTPS, and no
    // cookies are passed to MPlayer. Keep the validated host, path and query.
    auto playback_url=s.url;
    if(playback_url.rfind("https://",0)==0)playback_url.replace(0,5,"http");
    fifo_=c1::data()+"/bilibili/frame-"+std::to_string(getpid())+".y4m";
    if(mkfifo(fifo_.c_str(),0600))throw std::runtime_error("无法建立视频缓冲");
    video_=open(fifo_.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);if(video_<0){stop();throw std::runtime_error("无法打开视频缓冲");}
    fcntl(video_,F_SETPIPE_SZ,512*1024);
    int in[2],out[2];if(pipe2(in,O_CLOEXEC)){stop();throw std::runtime_error("无法创建控制管道");}if(pipe2(out,O_CLOEXEC)){close(in[0]);close(in[1]);stop();throw std::runtime_error("无法创建输出管道");}
    const char*silent=getenv("C1_BILI_SILENT");
    // Stock MPlayer routes HTTPS through libavformat, where -referrer and
    // -user-agent are not forwarded. Configure both HTTP transport paths.
    std::vector<std::string> args={"mplayer","-noconfig","all","-slave","-quiet","-identify","-noconsolecontrols","-nolirc","-nojoystick","-nomouseinput","-nosub","-noautosub","-osdlevel","0","-cache","512","-cache-min","10","-framedrop","-vo","null","-ao",silent&&std::string(silent)=="1"?"null":"media","-user-agent","Mozilla/5.0","-referrer","https://www.bilibili.com/","-lavfstreamopts","user_agent=Mozilla/5.0,referer=https://www.bilibili.com/",playback_url};
    std::vector<char*>av;for(auto&a:args)av.push_back(a.data());av.push_back(nullptr);
    if(!screen::video_begin()){for(int fd:{in[0],in[1],out[0],out[1]})close(fd);stop();throw std::runtime_error("无法准备视频显示");}
    auto preload=c1::root()+"/streamplayer/c1max-yuv-pipe.so";pid_t parent=getpid();pid_=fork();
    if(pid_==0){setpgid(0,0);prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);setenv("LD_PRELOAD",preload.c_str(),1);setenv("C1_YUV_FIFO",fifo_.c_str(),1);setenv("C1_YUV_SCALE","1",1);dup2(in[0],0);dup2(out[1],1);dup2(out[1],2);for(int fd:{in[0],in[1],out[0],out[1]})close(fd);execv("/usr/bin/mplayer",av.data());_exit(127);}
    close(in[0]);close(out[1]);input_=in[1];output_=out[0];if(pid_<0){stop();throw std::runtime_error("无法启动播放器");}
    setpgid(pid_,pid_);fcntl(input_,F_SETFL,O_NONBLOCK);fcntl(output_,F_SETFL,O_NONBLOCK);screen::playing=true;screen::tap=false;
    started_=last_frame_=last_query_=last_stats_=screen::tick();
}
void Player::stop(){
    if(pid_>0){std::fprintf(stderr,"[bilibili] stop frames=%llu position_ms=%d paused=%d\n",(unsigned long long)frames(),int(position*1000),int(paused));command("quit");pid_t p=pid_;pid_=-1;kill(-p,SIGTERM);bool done=false;for(int i=0;i<25;i++){if(waitpid(p,nullptr,WNOHANG)==p){done=true;break;}usleep(10000);}if(!done){kill(-p,SIGKILL);while(waitpid(p,nullptr,0)<0&&errno==EINTR){}}}
    for(int fd:{input_,output_,video_})if(fd>=0)close(fd);input_=output_=video_=-1;if(!fifo_.empty())unlink(fifo_.c_str());fifo_.clear();screen::playing=false;screen::video_end();
}
void Player::pause(){if(!active()||!loaded||!command("pause",""))return;paused=!paused;last_frame_=screen::tick();}
void Player::seek(double seconds){if(!active()||!loaded||!std::isfinite(seconds))return;position=std::clamp(seconds,0.0,double(std::max(0,duration-1)));command("seek "+std::to_string(position)+" 2","pausing_keep ");last_frame_=screen::tick();}
void Player::poll(){
    if(!active())return;uint8_t bytes[8192];size_t budget=768*1024;ssize_t n;
    while(budget&&(n=read(video_,bytes,std::min(sizeof(bytes),budget)))>0){budget-=size_t(n);if(!reader_.feed(bytes,n,[&](auto rgb,int w,int h,int an,int ad){screen::video_frame(rgb,w,h,an,ad);loaded=true;last_frame_=screen::tick();})){error=reader_.error();break;}}
    char buf[4096];size_t remaining=32768;while(remaining&&(n=read(output_,buf,std::min(sizeof(buf),remaining)))>0){remaining-=n;lines_.append(buf,n);if(lines_.size()>32768)lines_.erase(0,lines_.size()-32768);}
    size_t at;while((at=lines_.find_first_of("\r\n"))!=std::string::npos){auto line=lines_.substr(0,at);lines_.erase(0,at+1);
        try{if(line.rfind("ANS_TIME_POSITION=",0)==0){auto v=std::stod(line.substr(18));if(std::isfinite(v)&&v>=0)position=v;}
            else if(line.rfind("ID_LENGTH=",0)==0){auto v=std::stod(line.substr(10));if(std::isfinite(v)&&v>0&&v<=86400)duration=v;}
            else if(line.rfind("C1_YUV_ERROR=",0)==0)error="视频格式或尺寸超出直播放范围";
            else if(line.find("Failed to resolve hostname")!=std::string::npos)error="DNS 解析失败，请检查设备 Wi-Fi";
            else if(line.find("403 Forbidden")!=std::string::npos||line.find("403: Forbidden")!=std::string::npos)error="视频服务器拒绝访问（403），请返回后重试";
            else if(line.find("MPlayer interrupted by signal")!=std::string::npos)error="播放器异常退出，请返回后重试";
        }catch(...){}
    }
    auto now=screen::tick();if(now-last_query_>500){command("get_time_pos");last_query_=now;}
    if(now-last_stats_>5000){std::fprintf(stderr,"[bilibili] frames=%llu elapsed_ms=%u position_ms=%d paused=%d\n",(unsigned long long)frames(),now-started_,int(position*1000),int(paused));last_stats_=now;}
    int status=0;bool exited=waitpid(pid_,&status,WNOHANG)==pid_;
    if(exited){pid_t old=pid_;pid_=-1;kill(-old,SIGTERM);if(error.empty()){if(!loaded)error="视频打开失败，请检查网络或重新选择视频";else if(!WIFEXITED(status)||WEXITSTATUS(status))error="播放器异常退出，请返回后重试";}}
    bool timedout=(!loaded&&now-started_>30000)||(loaded&&!paused&&now-last_frame_>15000);
    if(timedout&&error.empty())error="视频加载超时，请检查网络后重试";
    screen::video_refresh(paused);
    if(exited||timedout||!error.empty()){stop();ended=true;}
}
}
