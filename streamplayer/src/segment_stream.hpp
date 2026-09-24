#pragma once
#include "client.hpp"
#include "hls.hpp"
#include <mutex>
#include <thread>

// One bounded compressed segment, one producer and one decoder. Settings only
// change at a complete TS segment boundary; no second audio decoder is started.
class SegmentStream {
public:
    struct Change { int64_t at; StreamOptions options; };
    struct State { bool ready=false,done=false;int64_t start=0;std::string error,notice;std::vector<Change> changes; };
    SegmentStream(MediaClient client,Playback playback,std::string fifo);
    ~SegmentStream();
    void request(StreamOptions options);
    void position(int64_t ticks){std::lock_guard<std::mutex> lock(mutex_);position_=ticks;}
    State state();
    std::vector<Playback> stop();
private:
    MediaClient client_;Playback playback_;std::string fifo_;
    std::atomic<bool> cancel_{false};int64_t position_=0;
    std::mutex mutex_;StreamOptions requested_;State state_;std::vector<Playback> sessions_;
    std::thread thread_;
    void run();
    std::string fetch(const std::string&url);
    std::pair<hls::Playlist,std::string> playlist(const Playback&p,const StreamOptions&options);
};
