#include "pdf.hpp"
#include "net.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace crosspoint {
namespace {
constexpr size_t text_limit=4*1024*1024, cache_limit=64*1024*1024;
using Clock=std::chrono::steady_clock;
Json identity(const std::string& path){
    struct stat s{};if(stat(path.c_str(),&s)||!S_ISREG(s.st_mode))throw std::runtime_error("Cannot read PDF file");
#ifdef __APPLE__
    auto m=s.st_mtimespec,c=s.st_ctimespec;
#else
    auto m=s.st_mtim,c=s.st_ctim;
#endif
    return {{"version",2},{"path",std::filesystem::absolute(path).string()},{"size",s.st_size},{"device",s.st_dev},{"inode",s.st_ino},
            {"mtime",m.tv_sec},{"mtime_ns",m.tv_nsec},{"ctime",c.tv_sec},{"ctime_ns",c.tv_nsec}};
}
std::string cache_path(const Json& id){
    // Verify the full identity on read, so a hash collision is only a cache miss.
    uint64_t hash=14695981039346656037ULL;for(unsigned char c:id.dump()){hash^=c;hash*=1099511628211ULL;}
    char name[32];snprintf(name,sizeof name,"/%016llx.text",(unsigned long long)hash);
    return c1::data()+"/crosspoint/pdf-cache"+name;
}
void trim_cache(const std::string& dir,const std::string& keep){
    namespace fs=std::filesystem;std::error_code ec;uintmax_t total=0;
    struct File {fs::path path;fs::file_time_type time;uintmax_t size;};std::vector<File> files;
    for(const auto& e:fs::directory_iterator(dir,ec)){
        if(!e.is_regular_file(ec)||e.path().extension()!=".text")continue;
        auto size=e.file_size(ec);if(ec){ec.clear();continue;}total+=size;
        if(e.path()!=keep){auto time=e.last_write_time(ec);if(!ec)files.push_back({e.path(),time,size});ec.clear();}
    }
    std::sort(files.begin(),files.end(),[](const File&a,const File&b){return a.time<b.time;});
    for(auto& f:files){if(total<=cache_limit)break;if(fs::remove(f.path,ec))total-=f.size;ec.clear();}
}
std::string extract(const std::string& path,const std::atomic<bool>& cancelled){
    auto tool=c1::root()+"/crosspoint/c1max-pdftotext";
    auto maps=c1::root()+"/crosspoint/assets/xpdf-chinese-simplified";
    auto config=c1::data()+"/crosspoint/pdf.xpdfrc";
    // Xpdf needs CID maps for PDFs without an embedded ToUnicode map.
    // Absolute paths remain valid for app-store packages and rollback releases.
    if(maps.find_first_of("\"\n\r")!=std::string::npos)throw std::runtime_error("Invalid PDF resource path");
    c1::save_private(config,"cidToUnicode Adobe-GB1 \""+maps+"/Adobe-GB1.cidToUnicode\"\n"+
                           "cMapDir Adobe-GB1 \""+maps+"/CMap\"\n"+
                           "toUnicodeDir \""+maps+"/CMap\"\n");
    // Prepare argv before fork: extraction runs alongside LVGL on another thread.
    const char* args[]={tool.c_str(),"-cfg",config.c_str(),"-layout","-enc","UTF-8",path.c_str(),"-",nullptr};
    int pipefd[2];if(pipe(pipefd))throw std::runtime_error("Cannot create PDF parser pipe");
    fcntl(pipefd[0],F_SETFD,FD_CLOEXEC);fcntl(pipefd[1],F_SETFD,FD_CLOEXEC);
    pid_t pid=fork();
    if(!pid){dup2(pipefd[1],STDOUT_FILENO);int null=open("/dev/null",O_WRONLY);if(null>=0){dup2(null,STDERR_FILENO);close(null);}close(pipefd[0]);close(pipefd[1]);execv(args[0],const_cast<char*const*>(args));_exit(127);}
    close(pipefd[1]);if(pid<0){close(pipefd[0]);throw std::runtime_error("Cannot start PDF parser");}
    fcntl(pipefd[0],F_SETFL,O_NONBLOCK);std::string text,why;auto started=Clock::now();bool eof=false,reaped=false;int status=0;
    while(!eof||!reaped){
        if(cancelled.load()){why="Cancelled";break;}
        if(Clock::now()-started>std::chrono::minutes(3)){why="PDF extraction timed out";break;}
        pollfd p{pipefd[0],POLLIN,0};if(!eof)poll(&p,1,50);else usleep(10000);
        char bytes[8192];ssize_t n;
        while(!eof&&(n=read(pipefd[0],bytes,sizeof bytes))>0){
            if(text.size()+size_t(n)>text_limit){why="PDF exceeds the 4 MiB text limit";break;}text.append(bytes,n);
        }
        if(!why.empty())break;
        if(!eof&&n==0)eof=true;
        if(!eof&&n<0&&errno!=EINTR&&errno!=EAGAIN){why="Cannot read PDF parser output";break;}
        if(!reaped){auto r=waitpid(pid,&status,WNOHANG);if(r==pid)reaped=true;else if(r<0&&errno!=EINTR){why="PDF parser disappeared";break;}}
    }
    close(pipefd[0]);if(!reaped){kill(pid,SIGKILL);while(waitpid(pid,&status,0)<0&&errno==EINTR){}}
    if(!why.empty())throw std::runtime_error(why);
    if(!WIFEXITED(status)||WEXITSTATUS(status))throw std::runtime_error("PDF parser could not read this file");
    if(text.find_first_not_of(" \t\r\n\f")==std::string::npos)throw std::runtime_error("This PDF has no extractable text (scanned pages need OCR)");
    return text;
}
}
bool read_pdf(const std::string& path,std::string& text,std::string& error,const std::atomic<bool>& cancelled){
    text.clear();error.clear();auto start=Clock::now();bool hit=false;
    try{
        if(cancelled.load())throw std::runtime_error("Cancelled");
        auto id=identity(path);auto cache=cache_path(id);
        try{auto saved=c1::read_file(cache,text_limit+16384);auto end=saved.find('\n');
            if(end!=std::string::npos&&end<16384){auto meta=Json::parse(saved.substr(0,end));if(meta.at("identity")==id&&meta.at("bytes")==saved.size()-end-1){text=saved.substr(end+1);hit=!text.empty()&&text.size()<=text_limit;if(!hit)text.clear();}}
        }catch(...){}
        if(!hit){
            text=extract(path,cancelled);
            if(identity(path)!=id)throw std::runtime_error("PDF changed while opening; please open it again");
            if(cancelled.load())throw std::runtime_error("Cancelled");
            try{auto dir=std::filesystem::path(cache).parent_path();std::filesystem::create_directories(dir);chmod(dir.c_str(),0700);
                c1::save_private(cache,Json{{"identity",id},{"bytes",text.size()}}.dump()+"\n"+text);trim_cache(dir,cache);
            }catch(...){} // A read-only/full cache must not prevent reading a book.
        }
        if(cancelled.load())throw std::runtime_error("Cancelled");
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-start).count();
        fprintf(stderr,"[crosspoint] PDF cache=%s text=%zu load_ms=%lld\n",hit?"hit":"miss",text.size(),(long long)elapsed);
        return true;
    }catch(const std::exception& e){text.clear();error=e.what();return false;}
}
}
