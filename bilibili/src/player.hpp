#pragma once
#include "api.hpp"
#include "y4m.hpp"
#include <sys/types.h>
namespace bili {
class Player {
public:
    ~Player();
    void start(const Stream&stream);
    void stop();
    void poll();
    void pause();
    void seek(double seconds);
    bool active()const{return pid_>0;}
    bool paused=false,loaded=false,ended=false;
    double position=0;int duration=0;
    std::string error;
    uint64_t frames()const{return reader_.frames();}
private:
    pid_t pid_=-1;int input_=-1,output_=-1,video_=-1;
    std::string fifo_,lines_;
    Y4mReader reader_;
    uint32_t started_=0,last_frame_=0,last_query_=0,last_stats_=0;
    bool command(const std::string&text,const char*prefix="pausing_keep_force ");
};
}
