#include "reader.hpp"
#include "net.hpp"
#include <mobi.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace crosspoint {
namespace {
std::string lower(std::string s){for(char &c:s)c=char(std::tolower((unsigned char)c));return s;}
std::string capture(const std::vector<std::string> &args,size_t limit,std::string &error){int p[2];if(pipe(p)){error="Cannot create parser pipe";return {};}pid_t pid=fork();if(pid==0){dup2(p[1],STDOUT_FILENO);int null=open("/dev/null",O_WRONLY);if(null>=0)dup2(null,STDERR_FILENO);close(p[0]);close(p[1]);std::vector<char*> av;for(auto &s:args)av.push_back(const_cast<char*>(s.c_str()));av.push_back(nullptr);execvp(av[0],av.data());_exit(127);}close(p[1]);if(pid<0){close(p[0]);error="Cannot start book parser";return {};}std::string out;char buf[8192];ssize_t n;while((n=read(p[0],buf,sizeof buf))>0){if(out.size()+size_t(n)>limit){error="Book exceeds the 4 MiB text limit";kill(pid,SIGKILL);break;}out.append(buf,n);}close(p[0]);int status=0;while(waitpid(pid,&status,0)<0){}if(!error.empty()||!WIFEXITED(status)||WEXITSTATUS(status)){if(error.empty())error="Book parser could not read this file";return {};}return out;}
}

bool read_book(const std::string &path,std::string &text,std::string &error){
    text.clear();error.clear();
    auto dot=path.find_last_of('.');std::string ext=dot==std::string::npos?"":lower(path.substr(dot));
    if(ext==".txt"){try{text=c1::read_file(path,4*1024*1024);return true;}catch(const std::exception &e){error=e.what();return false;}}
    if(ext==".md"||ext==".markdown"){try{text=markdown_text(c1::read_file(path,4*1024*1024));return true;}catch(const std::exception &e){error=e.what();return false;}}
    if(ext==".pdf"){std::string tool=c1::root()+"/crosspoint/c1max-pdftotext";text=capture({tool,"-layout","-enc","UTF-8",path,"-"},4*1024*1024,error);if(text.empty()&&error.empty())error="This PDF has no extractable text";return error.empty();}
    if(ext==".azw3"||ext==".azw"||ext==".mobi"||ext==".prc"){
        MOBIData *book=mobi_init();if(!book){error="Not enough memory to open this Kindle book";return false;}
        if(mobi_load_filename(book,path.c_str())!=MOBI_SUCCESS){mobi_free(book);error="This Kindle file is invalid or DRM protected";return false;}
        MOBIRawml *raw=mobi_init_rawml(book);if(!raw){mobi_free(book);error="Not enough memory to parse this Kindle book";return false;}
        auto result=mobi_parse_rawml(raw,book);if(result==MOBI_SUCCESS){for(MOBIPart *part=raw->flow;part;part=part->next){if(part->data&&part->size)text.append((const char*)part->data,part->size);}if(text.empty())for(MOBIPart *part=raw->markup;part;part=part->next)if(part->data&&part->size)text.append((const char*)part->data,part->size);}
        mobi_free_rawml(raw);mobi_free(book);if(result!=MOBI_SUCCESS||text.empty()){error="Could not decode this Kindle book (DRM books are not supported)";return false;}text=html_text(text);return !text.empty();
    }
    error="Format is not supported";return false;
}

}
