#include "store.hpp"
#include "audio_client.h"
#include <mbedtls/sha256.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/prctl.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
namespace store {
namespace fs=std::filesystem;
std::string base(){auto*p=getenv("C1_STORE_BASE");return p?p:"/storage/apps/current";}
std::string home(){return c1::data()+"/appstore";}
const std::vector<App>&builtin(){static const std::vector<App>a={
{"streamplayer","StreamPlayer","Emby / Jellyfin 视频"},{"calendar","日历","日程与 ICS 订阅"},{"calculator","计算器","物理键盘四则运算"},{"settings","设置","Wi-Fi、屏幕与声音"},{"piano","钢琴","触屏钢琴"},{"nes","NES 游戏","InfoNES 与本地游戏库"},{"terminal","终端","Linux 命令行"},{"gomoku","五子棋","人机 / 双人对局"},{"pcsx4all","PCSX4all","PS1 游戏模拟器"},{"processing","Processing","交互绘图与程序编辑"},{"dosbox","DOSBox","DOS 游戏模拟器"},{"airtune","Airtune","网络电台与本地收藏"},{"crosspoint","CrossPoint","电子书与 OPDS 书库"},{"camera","拍立得","相机、滤镜与相册"},{"mail","邮件","POP3 / SMTP 邮件"},{"bilibili","Bilibili","B 站视频"},{"hidpilot","HID 键鼠","USB 键盘与触控板"},{"moonpilot","MoonPilot AI","Moonlight 与 AI 远程操作"},{"tox","Tox 聊天","照片、语音与点对点聊天"},{"appstore","应用商店","选择安装、单独更新与首页管理"}};return a;}
namespace {
constexpr uint64_t max_bundle=128*1024*1024;
bool valid_id(const std::string&s){return std::regex_match(s,std::regex("[a-z][a-z0-9-]{0,31}"));}
bool hash_ok(const std::string&s){return std::regex_match(s,std::regex("[a-f0-9]{64}"));}
std::vector<int> version(const std::string&s){if(!std::regex_match(s,std::regex("[0-9]{1,5}\\.[0-9]{1,5}\\.[0-9]{1,5}")))throw std::runtime_error("无效版本号");std::vector<int>v;std::stringstream in(s);std::string p;while(std::getline(in,p,'.'))v.push_back(std::stoi(p));return v;}
std::string sha(const std::string&path){std::ifstream in(path,std::ios::binary);if(!in)throw std::runtime_error("无法打开安装包");mbedtls_sha256_context ctx;mbedtls_sha256_init(&ctx);mbedtls_sha256_starts_ret(&ctx,0);char b[32768];while(in){in.read(b,sizeof b);mbedtls_sha256_update_ret(&ctx,reinterpret_cast<uint8_t*>(b),in.gcount());}unsigned char out[32];mbedtls_sha256_finish_ret(&ctx,out);mbedtls_sha256_free(&ctx);std::ostringstream s;for(auto c:out)s<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(c);return s.str();}
void dirs(){fs::create_directories(home()+"/packages");fs::create_directories(home()+"/generations");chmod(home().c_str(),0700);}
struct Lock{int fd=-1;Lock(){dirs();fd=open((home()+"/install.lock").c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);if(fd<0||flock(fd,LOCK_EX|LOCK_NB)){if(fd>=0)close(fd);throw std::runtime_error("另一个安装任务正在运行");}}~Lock(){if(fd>=0)close(fd);}};
std::string fresh(const std::string&prefix){std::vector<char>b(prefix.begin(),prefix.end());b.push_back(0);auto*p=mkdtemp(b.data());if(!p)throw std::runtime_error("无法创建安装目录");return p;}
void replace_link(const std::string&target,const std::string&link){unlink((link+".new").c_str());if(symlink(target.c_str(),(link+".new").c_str())||rename((link+".new").c_str(),link.c_str()))throw std::runtime_error("无法切换应用版本");int d=open(home().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(d>=0){fsync(d);close(d);}}
void stop_audio(const std::string&id){int owner=id=="piano"?C1_AUDIO_PIANO:id=="airtune"?C1_AUDIO_AIRTUNE:id=="streamplayer"?C1_AUDIO_STREAMPLAYER:0;if(owner)c1_audio_command(owner,C1_AUDIO_STOP,0,nullptr);}
void stop_tox(){
    auto path=c1::data()+"/tox/service.sock";if(!fs::exists(path))return;
    sockaddr_un a{};a.sun_family=AF_UNIX;if(path.size()>=sizeof a.sun_path)throw std::runtime_error("Tox socket path too long");std::copy(path.begin(),path.end(),a.sun_path);
    int fd=socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0);if(fd<0)throw std::runtime_error("无法停止 Tox 后台");
    if(connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof a)){close(fd);return;}
    std::string body="{\"stop\":true}";uint32_t size=body.size();std::string packet(reinterpret_cast<char*>(&size),4);packet+=body;
    size_t at=0;while(at<packet.size()){ssize_t n=send(fd,packet.data()+at,packet.size()-at,MSG_NOSIGNAL);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);throw std::runtime_error("Tox 后台停止失败");}at+=n;}close(fd);
    for(int i=0;i<40&&fs::exists(path);i++)usleep(50000);
    if(fs::exists(path))throw std::runtime_error("Tox 后台尚未退出，请稍后重试");
}
void commit(const std::map<std::string,Local>&state,bool seed=false){
    if(!seed&&!fs::exists(home()+"/current"))commit(load_state(),true);
    auto dir=fresh(home()+"/generations/menu-XXXXXX");try{
        const auto origin=fs::canonical(base());
        for(auto&e:fs::directory_iterator(origin))if(e.is_directory())fs::create_directory_symlink(fs::absolute(base()+"/"+e.path().filename().string()),dir+"/"+e.path().filename().string());
        Json j={{"schema",1},{"apps",Json::object()}},catalog={{"schema",1},{"platform","c1max-mipsel-linux"},{"apps",Json::array()}};std::string menu;
        auto entries=builtin();for(auto&[id,l]:state)if(std::none_of(entries.begin(),entries.end(),[&](auto&a){return a.id==id;}))entries.push_back({id,l.title.empty()?id:l.title,""});
        for(auto&a:entries){auto it=state.find(a.id);if(it==state.end())continue;auto&l=it->second;
            j["apps"][a.id]={{"installed",l.installed},{"visible",l.visible},{"version",l.version},{"revision",l.revision},{"path",l.path},{"title",a.title}};
            if(!l.installed)continue;
            if(!l.path.empty()){fs::remove(dir+"/"+a.id);fs::create_directory_symlink(l.path,dir+"/"+a.id);}
            catalog["apps"].push_back({{"id",a.id},{"version",l.version},{"revision",l.revision}});
            if(l.visible||a.id=="appstore"){
                auto exe=a.id=="nes"?"c1max-nes-browser":"c1max-"+a.id;
                auto line=a.title+"|"+home()+"/current/"+a.id+"/"+exe+"|"+(a.id=="nes"?"--roms":"")+"|||"+(a.id=="appstore"?"updates":a.id)+"\n";if(a.id=="appstore")menu=line+menu;else menu+=line;
            }
        }
        c1::save_private(dir+"/state.json",j.dump(2));c1::save_private(dir+"/catalog.json",catalog.dump(2));c1::save_private(dir+"/launcher-menu.txt",menu);
        if(fs::exists(home()+"/current"))replace_link(fs::canonical(home()+"/current").string(),home()+"/previous");
        replace_link(dir,home()+"/current");
    }catch(...){fs::remove_all(dir);throw;}
}
}
std::map<std::string,Local> load_state(){
    std::map<std::string,Local>out;
    if(fs::exists(home()+"/current/state.json")){
        auto j=Json::parse(c1::read_file(home()+"/current/state.json",65536));if(j.at("schema")!=1)throw std::runtime_error("本地应用记录损坏");
        for(auto it=j.at("apps").begin();it!=j.at("apps").end();++it){auto id=it.key();if(!valid_id(id))throw std::runtime_error("无效的本地应用 ID");out[id]={it->at("installed"),it->at("visible"),it->at("version"),it->at("revision"),it->at("path"),it->value("title",id)};}return out;
    }
    auto catalog=Json::parse(c1::read_file(base()+"/catalog.json",65536));for(auto&a:builtin())for(auto&entry:catalog.at("apps"))if(entry.at("id")==a.id){std::string exe=a.id=="nes"?"c1max-nes-browser":"c1max-"+a.id;out[a.id]={access((base()+"/"+a.id+"/"+exe).c_str(),X_OK)==0,true,entry.at("version"),entry.at("revision"),""};}return out;
}
std::vector<App>fetch_catalog(){
    auto r=c1::http("GET","https://api.github.com/repos/zhuzhe1983/C1Max-Apps/contents/store/catalog.json?ref=main",{"Accept: application/vnd.github.raw+json","User-Agent: C1Max-AppStore/0.1"});
    if(r.status!=200)throw std::runtime_error("应用目录暂不可用，HTTP "+std::to_string(r.status));auto j=Json::parse(r.body);
    if(j.at("schema")!=1||j.at("platform")!="c1max-mipsel-linux"||j.at("runtime")!=1||!j.at("apps").is_array()||j.at("apps").size()>64)throw std::runtime_error("应用目录不兼容");
    std::vector<App>out;std::vector<std::string>ids;
    for(auto&e:j.at("apps")){App a{e.at("id"),e.at("title"),e.at("description"),e.at("version"),e.at("revision"),e.at("url"),e.at("sha256"),e.at("size"),e.at("unpacked")};
        if(!valid_id(a.id)||a.id=="launcher"||!hash_ok(a.revision)||!hash_ok(a.sha256)||a.size<16||a.size>max_bundle||a.unpacked>max_bundle||a.title.size()>80||a.title.find_first_of("|\r\n")!=std::string::npos||a.description.size()>300||a.url.rfind("https://github.com/zhuzhe1983/C1Max-Apps/releases/download/",0)||a.url.find_first_of("\r\n\t ")!=std::string::npos||std::find(ids.begin(),ids.end(),a.id)!=ids.end())throw std::runtime_error("无效的应用目录条目");version(a.version);ids.push_back(a.id);out.push_back(std::move(a));
    }dirs();c1::save_private(home()+"/remote-catalog.json",r.body);return out;
}
std::string compare(const Local&l,const App&a){if(!l.installed)return "未安装";if(a.version.empty())return "已安装";if(version(l.version)<version(a.version))return "可更新";if(version(l.version)>version(a.version))return "本地版本较新";return l.revision==a.revision?"已是当前版本":"同版本，构建不同";}
void set_visible(const std::string&id,bool visible){Lock lock;auto s=load_state();auto it=s.find(id);if(it==s.end()||!it->second.installed||id=="appstore")throw std::runtime_error("此应用不能隐藏");it->second.visible=visible;commit(s);}
void uninstall(const std::string&id){Lock lock;auto s=load_state();auto it=s.find(id);if(it==s.end()||id=="appstore")throw std::runtime_error("不能卸载应用商店");if(id=="tox")stop_tox();stop_audio(id);it->second.installed=false;it->second.visible=false;commit(s);}
void rollback(){Lock lock;if(!fs::exists(home()+"/previous/state.json"))throw std::runtime_error("没有可回退的变更");auto old=fs::canonical(home()+"/previous").string(),now=fs::canonical(home()+"/current").string();replace_link(old,home()+"/current");replace_link(now,home()+"/previous");}
void unpack(const std::string&bundle,const std::string&dest,const App&a){
    if(fs::file_size(bundle)!=a.size||sha(bundle)!=a.sha256)throw std::runtime_error("安装包 SHA-256 校验失败");std::ifstream in(bundle,std::ios::binary);char magic[8];in.read(magic,8);if(std::string(magic,8)!="C1PKG01\n")throw std::runtime_error("无效安装包");
    unsigned char n[4];in.read(reinterpret_cast<char*>(n),4);uint32_t len=n[0]|uint32_t(n[1])<<8|uint32_t(n[2])<<16|uint32_t(n[3])<<24;if(len<2||len>128*1024)throw std::runtime_error("安装包清单过大");std::string h(len,'\0');in.read(h.data(),len);auto j=Json::parse(h);
    if(j.at("id")!=a.id||j.at("version")!=a.version||j.at("revision")!=a.revision||!j.at("files").is_array()||j.at("files").size()>512)throw std::runtime_error("安装包与目录不匹配");
    uint64_t total=0;std::vector<std::string>paths;
    for(auto&f:j.at("files")){std::string path=f.at("path"),digest=f.at("sha256");uint64_t size=f.at("size");int mode=f.at("mode");
        if(path.rfind(a.id+"/",0)||path.size()>240||path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/_.- ")!=std::string::npos||path.find("..")!=std::string::npos||path.find("//")!=std::string::npos||path.back()=='/'||!hash_ok(digest)||(mode!=0644&&mode!=0755)||size>max_bundle||total+size>a.unpacked||std::find(paths.begin(),paths.end(),path)!=paths.end())throw std::runtime_error("安装包路径或大小无效");paths.push_back(path);total+=size;
        auto output=dest+"/"+path;fs::create_directories(fs::path(output).parent_path());int fd=open(output.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,mode);if(fd<0)throw std::runtime_error("无法创建应用文件");
        uint64_t remain=size;char b[32768];bool ok=true;while(remain){size_t want=std::min<uint64_t>(remain,sizeof b);in.read(b,want);if(size_t(in.gcount())!=want){ok=false;break;}size_t at=0;while(at<want){ssize_t w=write(fd,b+at,want-at);if(w<0&&errno==EINTR)continue;if(w<=0){ok=false;break;}at+=w;}if(!ok)break;remain-=want;}if(fsync(fd))ok=false;close(fd);if(!ok||sha(output)!=digest)throw std::runtime_error("应用文件损坏或存储空间不足");
    }
    if(total!=a.unpacked||in.peek()!=EOF)throw std::runtime_error("安装包长度不匹配");
    auto manifest=Json::parse(c1::read_file(dest+"/"+a.id+"/manifest.json",4096));if(manifest.at("id")!=a.id||manifest.at("version")!=a.version||manifest.at("revision")!=a.revision)throw std::runtime_error("应用版本清单不匹配");
    if(access((dest+"/"+a.id+"/c1max-"+a.id).c_str(),X_OK))throw std::runtime_error("应用程序缺失");
}
void install(const App&a,std::atomic<bool>&cancel,const std::function<void(unsigned,const std::string&)>&progress){
    Lock lock;auto state=load_state();auto old=state[a.id];if(old.installed&&version(old.version)>version(a.version))throw std::runtime_error("远端版本较旧，拒绝降级");
    struct statvfs space{};if(statvfs(home().c_str(),&space)||uint64_t(space.f_bavail)*space.f_frsize<a.size+a.unpacked+8*1024*1024)throw std::runtime_error("存储空间不足");
    auto stage=fresh(home()+"/packages/install-XXXXXX");auto bundle=stage+"/download.c1pkg";pid_t child=-1;bool committed=false;
    try{
        progress(0,"正在下载 "+a.title);std::vector<std::string>args={"/usr/bin/wget","--no-netrc","--no-hsts","--no-cookies","--no-proxy","--tries=2","--timeout=20","--max-redirect=10","--ca-certificate="+base()+"/shared/ca-certificates.crt","-q","-O",bundle,a.url};std::vector<char*>av;for(auto&s:args)av.push_back(s.data());av.push_back(nullptr);pid_t parent=getpid();child=fork();if(child<0)throw std::runtime_error("无法启动下载");if(!child){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);execv(av[0],av.data());_exit(127);}
        auto start=std::chrono::steady_clock::now();int status=0;
        for(;;){auto done=waitpid(child,&status,WNOHANG);if(done==child){child=-1;break;}if(done<0&&errno!=EINTR)throw std::runtime_error("下载进程异常");uint64_t got=fs::exists(bundle)?fs::file_size(bundle):0;
            if(cancel||got>a.size||std::chrono::steady_clock::now()-start>std::chrono::minutes(10))throw std::runtime_error(cancel?"已取消下载":"下载超时或大小不符");progress(unsigned(got*80/a.size),"正在下载 "+a.title);usleep(100000);
        }
        if(!WIFEXITED(status)||WEXITSTATUS(status))throw std::runtime_error("下载失败，请检查 Wi-Fi / GitHub 连接");if(cancel)throw std::runtime_error("已取消下载");progress(85,"校验并安装…");unpack(bundle,stage,a);fs::remove(bundle);if(cancel)throw std::runtime_error("已取消安装");
        for(auto&e:fs::recursive_directory_iterator(stage))if(e.is_directory()){int d=open(e.path().c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(d<0)throw std::runtime_error("无法同步安装目录");int result=fsync(d);close(d);if(result)throw std::runtime_error("无法同步安装目录");}
        for(auto&p:{stage,home()+"/packages"}){int d=open(p.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(d<0)throw std::runtime_error("无法同步安装目录");int result=fsync(d);close(d);if(result)throw std::runtime_error("无法同步安装目录");}
        if(a.id=="tox")stop_tox();stop_audio(a.id);state[a.id]={true,true,a.version,a.revision,stage+"/"+a.id,a.title};commit(state);committed=true;progress(100,"已安装，返回首页即可使用");
    }catch(...){if(child>0){kill(child,SIGKILL);while(waitpid(child,nullptr,0)<0&&errno==EINTR){}}if(!committed)fs::remove_all(stage);throw;}
}
}
