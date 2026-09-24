#include "transfer.hpp"
#include "net.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <stdexcept>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <unistd.h>

namespace crosspoint {
namespace {
using Clock=std::chrono::steady_clock;
struct Temp {
    std::string dir;
    explicit Temp(const std::string& parent) {
        std::string pattern=parent+"/.crosspoint-transfer-XXXXXX";
        std::vector<char> name(pattern.begin(),pattern.end());name.push_back(0);
        if(!mkdtemp(name.data()))throw std::runtime_error("Cannot create download temporary directory");
        dir=name.data();
    }
    ~Temp(){for(auto name:{"/body","/log","/config"})unlink((dir+name).c_str());rmdir(dir.c_str());}
};
std::string base64(const std::string& s) {
    static const char* chars="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned value=0,bits=0;std::string out;
    for(unsigned char c:s){value=(value<<8)|c;bits+=8;while(bits>=6){bits-=6;out+=chars[(value>>bits)&63];}}
    if(bits)out+=chars[(value<<(6-bits))&63];while(out.size()%4)out+='=';return out;
}
struct Headers {int status=0;uint64_t length=0;std::string location;};
Headers read_headers(const std::string& file) {
    Headers h;std::ifstream in(file);std::string line;
    while(std::getline(in,line)) {
        bool response_header=!line.empty()&&(line[0]==' '||line[0]=='\t');
        auto a=line.find_first_not_of(" \t");if(a==std::string::npos)continue;line.erase(0,a);
        if(line.rfind("HTTP/",0)==0){std::istringstream s(line);std::string protocol;s>>protocol>>h.status;h.location.clear();h.length=0;continue;}
        // wget also prints an unindented "Location: ... [following]" progress
        // message. Only the indented --server-response block is an HTTP header.
        if(!response_header)continue;
        auto colon=line.find(':');if(colon==std::string::npos)continue;
        auto key=line.substr(0,colon);for(char& c:key)c=char(std::tolower((unsigned char)c));
        auto value=line.substr(colon+1);auto b=value.find_first_not_of(" \t");if(b==std::string::npos)continue;value.erase(0,b);
        while(!value.empty()&&(value.back()=='\r'||value.back()==' '||value.back()=='\t'))value.pop_back();
        if(key=="location")h.location=value;
        if(key=="content-length"&&!value.empty()&&value.size()<20&&value.find_first_not_of("0123456789")==std::string::npos)h.length=strtoull(value.c_str(),nullptr,10);
    }
    return h;
}
uint64_t file_size(const std::string& p){struct stat s{};return stat(p.c_str(),&s)==0?uint64_t(s.st_size):0;}
std::string transfer(const std::string& initial,const Server& server,Temp& tmp,uint64_t limit,
                     unsigned seconds,const std::atomic<bool>& cancel,Progress& progress) {
    auto url=resolve_url(initial,"");const auto auth_origin=url_origin(server.url);
    const auto deadline=Clock::now()+std::chrono::seconds(seconds);progress.saving=false;
    long max_fd=sysconf(_SC_OPEN_MAX);if(max_fd<0)max_fd=1024;
    for(unsigned hop=0;hop<6;++hop) {
        if(cancel.load())throw std::runtime_error("Cancelled");
        std::string cfg="check_certificate = on\nmax_redirect = 0\ntries = 1\ntimeout = 12\n";
        cfg+="header = User-Agent: CrossPoint-C1Max/0.3\n";
        // Credentials never follow a redirect or an acquisition to another origin.
        if(!server.user.empty()&&url_origin(url)==auth_origin)cfg+="header = Authorization: Basic "+base64(server.user+":"+server.password)+"\n";
        c1::save_private(tmp.dir+"/config",cfg);
        int output=open((tmp.dir+"/body").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
        int log=open((tmp.dir+"/log").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
        if(output<0||log<0){if(output>=0)close(output);if(log>=0)close(log);throw std::runtime_error("Cannot open download file");}
        std::vector<std::string> args={"wget","--config="+tmp.dir+"/config","--no-netrc","--no-hsts","--no-cookies","--no-proxy",
            "--no-verbose","--server-response","--output-document=-","--ca-certificate="+c1::root()+"/shared/ca-certificates.crt",url};
        std::vector<char*> av;for(auto& s:args)av.push_back(s.data());av.push_back(nullptr);
        pid_t parent=getpid(),pid=fork();
        if(pid==0) {
#ifdef __linux__
            prctl(PR_SET_PDEATHSIG,SIGKILL);if(getppid()!=parent)_exit(1);
#endif
            dup2(output,1);dup2(log,2);
            int nullfd=open("/dev/null",O_RDONLY);if(nullfd>=0)dup2(nullfd,0);
            for(int fd=3;fd<max_fd;++fd)close(fd);
            rlimit cap{rlim_t(limit),rlim_t(limit)};setrlimit(RLIMIT_FSIZE,&cap);
            execvp(av[0],av.data());_exit(127);
        }
        close(output);close(log);
        if(pid<0)throw std::runtime_error("Cannot start network request");
        int status=0;bool aborted=false;std::string why;
        for(;;) {
            pid_t done=waitpid(pid,&status,WNOHANG);
            if(done==pid)break;
            if(done<0&&errno!=EINTR){why="Cannot wait for download";aborted=true;break;}
            progress.bytes=file_size(tmp.dir+"/body");
            auto h=read_headers(tmp.dir+"/log");if(h.length)progress.total=std::min<uint64_t>(h.length,UINT32_MAX);
            if(cancel.load()){why="Cancelled";aborted=true;break;}
            if(Clock::now()>deadline){why="Server timed out; try again";aborted=true;break;}
            if(h.length>limit||progress.bytes.load()>limit){why="Download exceeds the size limit";aborted=true;break;}
            if(file_size(tmp.dir+"/log")>65536){why="Invalid server response";aborted=true;break;}
            usleep(60000);
        }
        if(aborted){kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}throw std::runtime_error(why);}
        if(cancel.load())throw std::runtime_error("Cancelled");
        auto h=read_headers(tmp.dir+"/log");progress.bytes=file_size(tmp.dir+"/body");
        if(h.status==301||h.status==302||h.status==303||h.status==307||h.status==308) {
            if(h.location.empty())throw std::runtime_error("Redirect has no destination");
            auto next=resolve_url(url,h.location);
            if(url.rfind("https://",0)==0&&next.rfind("http://",0)==0)throw std::runtime_error("Refusing an insecure HTTPS redirect");
            url=next;progress.bytes=0;continue;
        }
        if(h.status==401||h.status==403)throw std::runtime_error("Access denied; check the server login");
        if(h.status!=200)throw std::runtime_error(h.status?"Server returned HTTP "+std::to_string(h.status):"Network/TLS failed; check Wi-Fi and clock");
        if(progress.bytes.load()>limit||h.length>limit)throw std::runtime_error("Download exceeds the size limit");
        if(!WIFEXITED(status)||WEXITSTATUS(status)!=0)throw std::runtime_error("Incomplete download; try again");
        if(h.length&&h.length!=progress.bytes.load())throw std::runtime_error("Incomplete server response");
        return url;
    }
    throw std::runtime_error("Too many redirects");
}
void validate_book(const std::string& p,const std::string& ext) {
    std::ifstream f(p,std::ios::binary);char bytes[80]{};f.read(bytes,sizeof(bytes));auto n=f.gcount();
    if(n==0)throw std::runtime_error("The server returned an empty book");
    if(ext==".epub"&&(n<4||memcmp(bytes,"PK\003\004",4)!=0))throw std::runtime_error("The downloaded file is not an EPUB");
    if(ext==".pdf"&&(n<5||memcmp(bytes,"%PDF-",5)!=0))throw std::runtime_error("The downloaded file is not a PDF");
    if((ext==".mobi"||ext==".azw3")&&(n<68||memcmp(bytes+60,"BOOKMOBI",8)!=0))throw std::runtime_error("The downloaded file is not a Kindle book");
    std::string prefix(bytes,size_t(n));for(char& c:prefix)c=char(std::tolower((unsigned char)c));
    if(prefix.find("<!doctype html")!=std::string::npos||prefix.find("<html")!=std::string::npos)throw std::runtime_error("The server returned an HTML page instead of a book");
}
}
Document fetch_document(const std::string& url,const Server& server,const std::atomic<bool>& cancel,Progress& progress) {
    Temp tmp("/tmp");progress.bytes=0;progress.total=0;
    auto final_url=transfer(url,server,tmp,1024*1024,30,cancel,progress);
    return {c1::read_file(tmp.dir+"/body",1024*1024),final_url};
}
std::string download_book(const OpdsItem& book,const Acquisition& format,const Server& server,
                          const std::string& directory,const std::atomic<bool>& cancel,Progress& progress) {
    constexpr uint64_t limit=128*1024*1024;
    if(format.size>limit)throw std::runtime_error("Book exceeds the 128 MiB download limit");
    Temp tmp(directory);progress.bytes=0;progress.total=format.size;
    // A slow but progressing transfer must not fail after the old fixed 3 min.
    // wget still enforces its 12 s network read timeout; cancellation stays live.
    transfer(format.url,server,tmp,limit,1800,cancel,progress);
    progress.saving=true;
    validate_book(tmp.dir+"/body",format.extension);
    if(cancel.load())throw std::runtime_error("Cancelled");
    int fd=open((tmp.dir+"/body").c_str(),O_RDONLY);bool synced=fd>=0&&fsync(fd)==0;if(fd>=0)close(fd);
    if(!synced)throw std::runtime_error("Cannot sync the downloaded book");
    auto name=book_filename(book,format),base=name.substr(0,name.size()-format.extension.size());
    for(unsigned suffix=0;suffix<1000;++suffix) {
        auto dest=directory+"/"+base+(suffix?" ("+std::to_string(suffix)+")":"")+format.extension;
        if(link((tmp.dir+"/body").c_str(),dest.c_str())==0)return dest;
        if(errno!=EEXIST)throw std::runtime_error("Cannot save book: "+std::string(strerror(errno)));
    }
    throw std::runtime_error("Too many copies of this book");
}
}
