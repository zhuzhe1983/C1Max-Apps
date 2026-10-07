#include "reader.hpp"
#include "pdf.hpp"
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

}

bool read_book(const std::string &path,std::string &text,std::string &error){
    text.clear();error.clear();
    auto dot=path.find_last_of('.');std::string ext=dot==std::string::npos?"":lower(path.substr(dot));
    if(ext==".txt"){try{text=c1::read_file(path,4*1024*1024);return true;}catch(const std::exception &e){error=e.what();return false;}}
    if(ext==".md"||ext==".markdown"){try{text=markdown_text(c1::read_file(path,4*1024*1024));return true;}catch(const std::exception &e){error=e.what();return false;}}
    if(ext==".pdf"){std::atomic<bool> cancelled{false};return read_pdf(path,text,error,cancelled);}
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
