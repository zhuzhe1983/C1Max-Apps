#pragma once
#include "api.hpp"
#include "y4m.hpp"
#include "cast_relay.hpp"
#include "output_mode.hpp"
#include <sys/types.h>
namespace bili {
class Player {
public:
    ~Player();
    void start(const Stream&stream,const std::string &title="Bilibili");
    void stop();
    void poll();
    void pause();
    void seek(double seconds);
    bool active()const{return pid_>0||remote_;}
    bool remote_only()const{return remote_&&mode_==casting::OutputMode::Remote;}
    bool casting()const{return remote_;}
    casting::OutputMode mode()const{return mode_;}
    std::string output_label()const;
    int remote_volume()const{return remote_volume_;}
    void set_remote_volume(int value);
    bool paused=false,loaded=false,ended=false;
    double position=0;int duration=0;
    std::string error;
    uint64_t frames()const{return reader_.frames();}
private:
    CastRelay relay_;
    casting::OutputMode mode_=casting::OutputMode::Local;
    bool remote_=false,remote_loaded_=false,pending_pause_=false;
    double pending_seek_=-1;
    int remote_volume_=-1;
    uint64_t content_id_=0;
    uint32_t last_cast_=0,cast_started_=0;
    void local_start(const Stream &stream,bool silent);
    void local_stop();
    void remote_poll();
    pid_t pid_=-1;int input_=-1,output_=-1,video_=-1;
    std::string fifo_,lines_;
    Y4mReader reader_;
    uint32_t started_=0,last_frame_=0,last_query_=0,last_stats_=0;
    bool command(const std::string&text,const char*prefix="pausing_keep_force ");
};
}
