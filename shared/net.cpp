#include "net.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <unistd.h>
namespace c1 {
static std::atomic<bool> canceled{false};
static std::atomic<int> request_timeout{20000};
void cancel_requests(){canceled=true;}
void reset_requests(int timeout_ms){request_timeout=timeout_ms;canceled=false;}
std::string root(){auto p=getenv("C1_APPS_ROOT"); return p?p:"/storage/apps/current";}
std::string data(){auto p=getenv("C1_APPS_DATA"); return p?p:"/storage/apps/data";}
std::string read_file(const std::string&p,size_t limit){
    std::ifstream f(p,std::ios::binary); if(!f) throw std::runtime_error("File unavailable: "+p);
    std::string out; char buf[4096]; while(f){f.read(buf,sizeof(buf));out.append(buf,f.gcount());if(out.size()>limit)throw std::runtime_error("Response exceeds size limit");}return out;
}
void save_private(const std::string&p,const std::string&s){
    std::string tmp=p+".XXXXXX"; std::vector<char> n(tmp.begin(),tmp.end());n.push_back(0);
    int fd=mkstemp(n.data()); if(fd<0)throw std::runtime_error("Cannot create configuration");
    size_t off=0; while(off<s.size()){ssize_t w=write(fd,s.data()+off,s.size()-off);if(w<=0){close(fd);unlink(n.data());throw std::runtime_error("Cannot save configuration");}off+=w;}
    if(fsync(fd)<0){close(fd);unlink(n.data());throw std::runtime_error("Cannot sync configuration");}close(fd);
    if(rename(n.data(),p.c_str())){unlink(n.data());throw std::runtime_error("Cannot replace configuration");}
}
std::string encode(const std::string&s){std::string o;const char*h="0123456789ABCDEF";for(unsigned char c:s){if(isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')o+=c;else{o+='%';o+=h[c>>4];o+=h[c&15];}}return o;}
std::string origin(const std::string&u){
    if(u.rfind("http://",0)&&u.rfind("https://",0))throw std::runtime_error("Use an http:// or https:// server address");
    if(u.find_first_of("\r\n\t ")!=std::string::npos)throw std::runtime_error("Invalid server address");
    auto p=u.find('/',u.find("://")+3); auto o=u.substr(0,p);
    if(o.find('@')!=std::string::npos||o.find('?')!=std::string::npos||o.find('#')!=std::string::npos||o.size()<=u.find("://")+3)throw std::runtime_error("Invalid server address");
    return o;
}
std::string resolve(const std::string&base,const std::string&p){
    auto o=origin(base); if(p.rfind("http://",0)==0||p.rfind("https://",0)==0){if(origin(p)!=o)throw std::runtime_error("Cross-server playback URL refused");return p;}
    if(p.rfind("//",0)==0)throw std::runtime_error("Invalid playback URL");
    return (!p.empty()&&p[0]=='/')?o+p:base+"/"+p;
}
struct TempDir {std::string path; TempDir(){char t[]="/tmp/c1apps-http-XXXXXX";char*p=mkdtemp(t);if(!p)throw std::runtime_error("Cannot create request directory");path=p;}~TempDir(){for(auto s:{"/config","/body","/log"})unlink((path+s).c_str());rmdir(path.c_str());}};
Response http(const std::string&method,const std::string&url,const std::vector<std::string>&headers,const std::string&body,const std::atomic<bool>*cancel){
    origin(url);TempDir tmp;
    std::string cfg="check_certificate = on\nmax_redirect = 0\ntries = 1\ntimeout = 8\n";
    for(auto &h:headers){if(h.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("Invalid HTTP header");cfg+="header = "+h+"\n";}
    save_private(tmp.path+"/config",cfg);save_private(tmp.path+"/body",body);
    int pipefd[2];if(pipe2(pipefd,O_CLOEXEC))throw std::runtime_error("Cannot open request pipe");
    int log=open((tmp.path+"/log").c_str(),O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC,0600);
    std::vector<std::string> args={"wget","--config="+tmp.path+"/config","--no-netrc","--no-hsts","--no-cookies","--no-proxy","--server-response","--output-document=-","--method="+method,"--ca-certificate="+root()+"/shared/ca-certificates.crt"};
    if(method!="GET")args.push_back("--body-file="+tmp.path+"/body");args.push_back(url);
    std::vector<char*> av;for(auto&s:args)av.push_back(s.data());av.push_back(nullptr);
    pid_t parent=getpid(),pid=fork();if(pid==0){prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);dup2(pipefd[1],1);dup2(log,2);close(pipefd[0]);close(pipefd[1]);close(log);execvp("wget",av.data());_exit(127);}
    close(log);close(pipefd[1]);if(pid<0){close(pipefd[0]);throw std::runtime_error("Cannot start request");}
    fcntl(pipefd[0],F_SETFL,O_NONBLOCK);Response r;bool done=false,failed=false;
    auto start=std::chrono::steady_clock::now();while(!done){
        pollfd p{pipefd[0],POLLIN,0};poll(&p,1,100);char buf[8192];ssize_t n;
        while((n=read(pipefd[0],buf,sizeof(buf)))>0){r.body.append(buf,n);if(r.body.size()>1048576){failed=true;break;}}
        if(n==0)done=true;
        if(failed||canceled.load()||(cancel&&cancel->load())||std::chrono::steady_clock::now()-start>std::chrono::milliseconds(request_timeout.load())){failed=true;kill(pid,SIGKILL);break;}
    }close(pipefd[0]);int status=0;while(waitpid(pid,&status,0)<0&&errno==EINTR){}
    std::string logtext=read_file(tmp.path+"/log",65536);std::smatch match;
    std::regex code("HTTP/[0-9.]+ ([0-9]{3})");
    for(std::sregex_iterator it(logtext.begin(),logtext.end(),code),end;it!=end;++it){int status_code=std::stoi((*it)[1]);if(status_code>=200)r.status=status_code;}
    if(failed)throw std::runtime_error("Request timed out or response too large");
    if(!r.status)throw std::runtime_error("Network/TLS failed. Check Wi-Fi, server and clock.");
    if(!WIFEXITED(status)||WEXITSTATUS(status)!=0){if(r.status>=200&&r.status<300)throw std::runtime_error("Incomplete server response");}
    return r;
}
Json request(const std::string&m,const std::string&u,const std::vector<std::string>&h,const Json&body){auto r=http(m,u,h,body.is_null()?"":body.dump());if(r.status<200||r.status>=300)throw std::runtime_error("Server returned HTTP "+std::to_string(r.status));if(r.body.empty())return Json::object();return Json::parse(r.body);}
static void validate(const Json&j){
    if(j.at("schema")!=1||j.at("platform")!="c1max-mipsel-linux"||!j.at("apps").is_array()||j["apps"].size()>64)throw std::runtime_error("Incompatible app catalog");
    std::vector<std::string> ids;
    for(auto&a:j["apps"]){auto id=a.at("id").get<std::string>();auto v=a.at("version").get<std::string>();auto rev=a.at("revision").get<std::string>();if(!std::regex_match(id,std::regex("[a-z][a-z0-9-]{0,31}"))||!std::regex_match(v,std::regex("[0-9]{1,5}\\.[0-9]{1,5}\\.[0-9]{1,5}"))||!std::regex_match(rev,std::regex("[0-9a-f]{64}"))||std::find(ids.begin(),ids.end(),id)!=ids.end())throw std::runtime_error("Invalid app catalog");ids.push_back(id);}
}
static std::vector<int> version(std::string s){std::vector<int> a;size_t n;do{n=s.find('.');a.push_back(std::stoi(s.substr(0,n)));if(n!=std::string::npos)s.erase(0,n+1);}while(n!=std::string::npos);return a;}
Json check_updates(const Json&local,const Json&remote){validate(local);validate(remote);Json out=Json::array();for(auto&r:remote["apps"]){auto it=std::find_if(local["apps"].begin(),local["apps"].end(),[&](const Json&l){return l["id"]==r["id"];});std::string state="current";if(it==local["apps"].end())state="new";else if(version(r["version"])>version((*it)["version"]))state="update";else if(version(r["version"])==version((*it)["version"])&&r["revision"]!=(*it)["revision"])state="source differs";else if(version(r["version"])<version((*it)["version"]))state="local newer";out.push_back({{"id",r["id"]},{"version",r["version"]},{"state",state}});}return out;}
Json updates(){
    const std::string url="https://api.github.com/repos/zhuzhe1983/C1Max-Apps/contents/catalog.json?ref=main";
    auto r=http("GET",url,{"Accept: application/vnd.github.raw+json","User-Agent: C1Max-Apps/0.1.0"});
    if(r.status==404||r.status==401)throw std::runtime_error("Update source is private or not published yet");
    if(r.status==403||r.status==429)throw std::runtime_error("GitHub rate limit; try again later");
    if(r.status!=200)throw std::runtime_error("GitHub HTTP "+std::to_string(r.status));
    return check_updates(Json::parse(read_file(root()+"/catalog.json")),Json::parse(r.body));
}
}
