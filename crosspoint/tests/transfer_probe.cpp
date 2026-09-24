#include "transfer.hpp"
#include <chrono>
#include <iostream>
#include <thread>
int main(int argc,char**argv){
    if(argc<4)return 2;
    std::atomic<bool> cancel{false};crosspoint::Progress progress;
    crosspoint::Server server{"Test",argv[2],argc>4?argv[4]:"",argc>5?argv[5]:""};
    std::thread timer;if(argc>6)timer=std::thread([&]{std::this_thread::sleep_for(std::chrono::milliseconds(atoi(argv[6])));cancel=true;});
    int result=0;
    try{
        if(std::string(argv[1])=="feed"){
            auto d=crosspoint::fetch_document(argv[2],server,cancel,progress);crosspoint::Feed f;std::string error;
            if(!crosspoint::parse_feed(d.body,d.url,f,error))throw std::runtime_error(error);
            std::cout<<"FEED "<<f.items.size()<<" "<<f.url<<"\n";
        }else{
            crosspoint::OpdsItem b;b.title="book";crosspoint::Acquisition f{argv[2],"application/epub+zip",".epub",0};
            std::cout<<crosspoint::download_book(b,f,server,argv[3],cancel,progress)<<"\n";
        }
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";result=1;}
    if(timer.joinable())timer.join();return result;
}
