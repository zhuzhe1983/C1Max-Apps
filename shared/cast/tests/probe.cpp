#include "../internal.hpp"
#include <fstream>
#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>
int main(int argc,char**argv){
    signal(SIGPIPE,SIG_IGN);
    std::unique_ptr<casting::Receiver> r;bool loaded=false;double restore_volume=-1;
    try{
        if(argc==2&&std::string(argv[1])=="--discover"){std::cout<<casting::Json(casting::discover()).dump(2)<<"\n";return 0;}
        if((argc!=4||std::string(argv[1])!="--connect")&&(argc!=5||std::string(argv[1])!="--exercise")){std::cerr<<"Usage: probe --connect DEVICE.json STATE_DIR | --exercise DEVICE.json STATE_DIR MEDIA.json\n";return 2;}
        std::ifstream file(argv[2]);casting::Json device;file>>device;r=casting::receiver(device.at("protocol"));r->connect(device,argv[3]);std::cout<<"connected "<<r->poll().dump()<<std::endl;
        if(argc==4)return 0;
        std::ifstream media_file(argv[4]);casting::Json media;media_file>>media;
        if(media.contains("test_volume")){restore_volume=r->poll().value("volume",-1.0);if(restore_volume>=0)r->command("volume",media.at("test_volume"));}
        loaded=true;r->load(media);
        auto await_state=[&](const std::string&state){auto end=casting::now_ms()+30000;casting::Json p;do{p=r->poll();std::cout<<p.dump()<<std::endl;if(p.value("state",std::string())==state&&(state!="playing"||p.value("position",0.0)>=1))return;std::this_thread::sleep_for(std::chrono::milliseconds(400));}while(casting::now_ms()<end);throw std::runtime_error("receiver did not reach "+state);};
        await_state("playing");std::this_thread::sleep_for(std::chrono::seconds(2));r->command("pause",0);await_state("paused");
        if(!media.value("live",false)){r->command("seek",4);await_state("paused");}
        r->command("play",0);await_state("playing");
        if(!media.value("live",false))r->command("seek",4);
        std::this_thread::sleep_for(std::chrono::seconds(3));
        r->command("stop",0);await_state("idle");loaded=false;if(restore_volume>=0)r->command("volume",restore_volume);std::cout<<"PASS load/play/pause/resume/stop"<<std::endl;return 0;
    }catch(const std::exception&e){if(loaded&&r)try{r->command("stop",0);}catch(...){}if(restore_volume>=0&&r)try{r->command("volume",restore_volume);}catch(...){}std::cerr<<e.what()<<"\n";return 1;}
}
