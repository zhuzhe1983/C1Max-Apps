#include "epub.hpp"
#include <chrono>
#include <iostream>
int main(int n,char** v){
    if(n<2)return 2;
    std::atomic<bool> cancel{n>3&&std::string(v[3])=="cancel"};
    try{
        auto start=std::chrono::steady_clock::now();crosspoint::EpubBook book;book.open(v[1],cancel);
        auto opened=std::chrono::steady_clock::now();auto index=n>2?std::stoul(v[2]):0;
        auto text=book.chapter(index,cancel);auto end=std::chrono::steady_clock::now();
        std::cout<<"chapters="<<book.chapter_count()<<" bytes="<<text.size()<<" index_ms="<<std::chrono::duration<double,std::milli>(opened-start).count()<<" chapter_ms="<<std::chrono::duration<double,std::milli>(end-opened).count()<<"\n";
        if(n>3&&std::string(v[3])=="text")std::cout<<text;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
