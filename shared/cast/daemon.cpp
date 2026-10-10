#include "internal.hpp"
#include "../cast.hpp"
#include "../net.hpp"
#include "../power_lock.h"
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <poll.h>
#include <sstream>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
using namespace casting;
namespace {
volatile sig_atomic_t quit=0;
void stop_signal(int){quit=1;}
std::string identity(pid_t pid){if(pid<=1)return {};std::string s;try{s=c1::read_file("/proc/"+std::to_string(pid)+"/stat",4096);}catch(...){return {};}auto i=s.rfind(')');if(i==std::string::npos||s.substr(i+2,1)=="Z")return {};std::istringstream f(s.substr(i+2));std::string token;for(int field=3;field<=22;field++)if(!(f>>token))return {};return token;}
struct Job {Json request;pid_t pid=0;std::string stamp;};
class Service {
    std::mutex mutex;std::condition_variable changed;std::deque<Job> queue;std::atomic<bool> exiting{false};std::thread worker;
    Json state{{"ok",true},{"connected",false},{"busy",false},{"state","idle"},{"owner",""},{"title",""},{"error",""},{"devices",Json::array()}};
    std::string dir;std::unique_ptr<Receiver> sink;pid_t owner_pid=0;std::string owner_stamp;Fd power;int64_t last_power=0;uint64_t serial=0;pid_t supervisor=0;std::string supervisor_stamp;
    void merge(const Json&patch){std::lock_guard<std::mutex>lock(mutex);state.update(patch);}
    Json snapshot(){std::lock_guard<std::mutex>lock(mutex);return state;}
    void release(){power.reset();}
    void keep_awake(){auto s=snapshot();bool playing=s.value("state",std::string())=="playing"||s.value("state",std::string())=="buffering";if(!playing){release();return;}if(now_ms()-last_power<5000)return;last_power=now_ms();if(power.n<0){power.reset(socket(AF_UNIX,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0));sockaddr_un a{};a.sun_family=AF_UNIX;strcpy(a.sun_path,"/dev/socket/PowerLock");if(power.n>=0&&::connect(power.n,(sockaddr*)&a,sizeof a))power.reset();}if(power.n>=0&&powerlock_command_timeout(power.n,"suslock",100))power.reset();}
    void close_receiver(){if(sink)try{sink->command("stop",0);}catch(...){}sink.reset();release();owner_pid=0;owner_stamp.clear();merge({{"connected",false},{"state","idle"},{"owner",""},{"title",""},{"position",0},{"duration",0}});}
    void execute(const Job&j){const auto&r=j.request;std::string action=r.at("action");
        if(action=="scan"){merge({{"devices",discover()}});return;}
        if(action=="select"){
            Json device;auto id=r.value("id",std::string());auto found=snapshot();for(auto&d:found["devices"])if(d.value("id",std::string())==id)device=d;
            if(!device.is_object()||!device.value("supported",false))throw std::runtime_error("此接收设备尚不支持连接");close_receiver();auto next=receiver(device.at("protocol"));next->connect(device,dir);sink=std::move(next);merge({{"connected",true},{"device",device},{"error",""}});c1::save_private(dir+"/last-device.json",device.dump());return;
        }
        if(action=="disconnect"){close_receiver();return;}
        if(action=="forget"){auto s=snapshot();close_receiver();if(s.contains("device")){Json pins;try{pins=Json::parse(c1::read_file(dir+"/pins.json"));}catch(...){}if(pins.is_object()){pins.erase(s["device"].value("id",std::string()));c1::save_private(dir+"/pins.json",pins.dump());}}unlink((dir+"/last-device.json").c_str());merge({{"device",Json::object()}});return;}
        if(action=="volume_step"){
            if(!sink||owner_pid<=0)return;
            auto s=snapshot();if(!s.contains("volume"))return;
            double delta=r.value("value",0.0);if(!std::isfinite(delta)||std::abs(delta)>1)return;
            double level=r.value("mute",false)?0:std::clamp(s.value("volume",0.5)+delta,0.0,1.0);
            sink->command("volume",level);merge({{"volume",level}});return;
        }
        if(!sink)throw std::runtime_error("请先在设置选择投屏设备");
        std::string owner=r.value("owner",std::string());auto s=snapshot();
        if(action=="load"){
            if(owner.empty()||owner.size()>64||j.stamp.empty()||identity(j.pid)!=j.stamp)throw std::runtime_error("播放应用已经退出");
            if(!media_url(r.value("url",std::string()))||r.value("title",std::string()).size()>512||r.value("mime",std::string()).size()>128)throw std::runtime_error("无效投屏内容");
            owner_pid=j.pid;owner_stamp=j.stamp;merge({{"owner",owner},{"content_id",r.value("content_id",uint64_t(0))},{"title",r.value("title",std::string())},{"state","buffering"},{"position",r.value("position",0.0)},{"duration",0},{"error",""}});sink->load(r);return;
        }
        if(owner!="settings"&&(owner.empty()||owner!=s.value("owner",std::string())||j.pid!=owner_pid||j.stamp!=owner_stamp))return; // stale owner must never interrupt its successor
        sink->command(action,r.value("value",0.0));if(action=="stop"){owner_pid=0;owner_stamp.clear();merge({{"state","idle"},{"owner",""},{"title",""}});release();}
    }
    void run(){int64_t last_poll=0;while(!exiting){if(supervisor>0&&identity(supervisor)!=supervisor_stamp){quit=1;break;}Job job;bool have=false;{std::unique_lock<std::mutex>lock(mutex);changed.wait_for(lock,std::chrono::milliseconds(100),[&]{return exiting||!queue.empty();});if(exiting)break;if(!queue.empty()){job=std::move(queue.front());queue.pop_front();have=true;state["busy"]=true;}}
        if(have){try{execute(job);merge({{"error",""}});}catch(const std::exception&e){merge({{"error",std::string(e.what()).substr(0,256)}});if(job.request.value("action",std::string())=="load"){if(sink)try{sink->command("stop",0);}catch(...){}merge({{"state","error"}});}}merge({{"busy",false}});}
        if(sink&&now_ms()-last_poll>=1000){last_poll=now_ms();try{
            if(owner_pid>0&&identity(owner_pid)!=owner_stamp){sink->command("stop",0);owner_pid=0;owner_stamp.clear();merge({{"owner",""},{"title",""}});}
            auto p=sink->poll();if(snapshot().value("state",std::string())=="error")p.erase("state");merge(p);
        }catch(const std::exception&e){sink.reset();release();merge({{"connected",false},{"state","error"},{"error",std::string(e.what()).substr(0,256)}});}}
        keep_awake();
    }close_receiver();}
public:
    explicit Service(std::string directory):dir(std::move(directory)){try{supervisor=std::stoi(c1::read_file(c1::data()+"/launcher/run.lock/pid",32));supervisor_stamp=identity(supervisor);if(supervisor_stamp.empty())supervisor=0;}catch(...){supervisor=0;}worker=std::thread([this]{run();});}
    ~Service(){exiting=true;changed.notify_one();worker.join();}
    Json accept(Json r,pid_t pid){
        if(!r.is_object()||!r.contains("action")||!r["action"].is_string())return {{"ok",false},{"error","无效投屏请求"}};
        std::string action=r["action"];if(action=="status"){auto s=snapshot();return s;}
        if(action=="shutdown"){quit=1;return {{"ok",true}};}
        static const std::vector<std::string> actions={"scan","select","disconnect","forget","load","play","pause","seek","stop","volume","volume_step"};if(std::find(actions.begin(),actions.end(),action)==actions.end())return {{"ok",false},{"error","未知投屏请求"}};
        auto stamp=identity(pid);std::lock_guard<std::mutex>lock(mutex);if(queue.size()>=8)return {{"ok",false},{"error","投屏操作正在排队"}};
        uint64_t accepted=0;if(action=="load")r["content_id"]=accepted=++serial;
        queue.push_back({std::move(r),pid,stamp});changed.notify_one();Json out=state;out["ok"]=true;out["accepted"]=true;out["accepted_id"]=accepted;out["busy"]=true;return out;
    }
};
}
int main(int argc,char**argv){
    if(argc==2&&!strcmp(argv[1],"--stop")){auto r=casting::call({{"action","shutdown"}});return r.value("ok",false)?0:1;}
    if(argc==2&&!strcmp(argv[1],"--discover")){try{std::cout<<Json(discover()).dump(2)<<"\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
    if(argc==3&&!strcmp(argv[1],"--request")){auto r=casting::call(Json::parse(argv[2]),true);std::cout<<r.dump(2)<<"\n";return r.value("ok",false)?0:1;}
    if(argc!=2||strcmp(argv[1],"--daemon"))return 2;
    signal(SIGTERM,stop_signal);signal(SIGINT,stop_signal);signal(SIGHUP,stop_signal);signal(SIGPIPE,SIG_IGN);umask(077);
    std::string dir=c1::data()+"/cast";if(mkdir(dir.c_str(),0700)&&errno!=EEXIST)return 1;
    Fd lock(open((dir+"/service.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC|O_NOFOLLOW,0600));if(lock.n<0||flock(lock.n,LOCK_EX|LOCK_NB))return 1;
    sockaddr_un a{};a.sun_family=AF_UNIX;auto path=dir+"/control.sock";if(path.size()>=sizeof a.sun_path)return 1;strcpy(a.sun_path,path.c_str());Fd server(socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC|SOCK_NONBLOCK,0));unlink(path.c_str());if(server.n<0||bind(server.n,(sockaddr*)&a,sizeof a)||chmod(path.c_str(),0600)||listen(server.n,8))return 1;
    {Service service(dir);while(!quit){pollfd p{server.n,POLLIN,0};if(poll(&p,1,200)<=0)continue;Fd client(accept4(server.n,nullptr,nullptr,SOCK_CLOEXEC|SOCK_NONBLOCK));if(client.n<0)continue;ucred cred{};socklen_t len=sizeof cred;if(getsockopt(client.n,SOL_SOCKET,SO_PEERCRED,&cred,&len)||cred.uid!=geteuid())continue;
        char b[16385];pollfd read{client.n,POLLIN,0};if(poll(&read,1,80)<=0)continue;ssize_t n=recv(client.n,b,sizeof b,MSG_TRUNC);if(n<=0||n>16384)continue;auto r=Json::parse(b,b+n,nullptr,false);Json reply;try{reply=service.accept(r,cred.pid);}catch(...){reply={{"ok",false},{"error","无效投屏请求"}};}auto data=reply.dump(-1,' ',false,Json::error_handler_t::replace);send(client.n,data.data(),data.size(),MSG_NOSIGNAL);
    }}unlink(path.c_str());return 0;
}
