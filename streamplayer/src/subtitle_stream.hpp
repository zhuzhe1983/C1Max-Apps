#pragma once
#include "client.hpp"
#include "subtitles.hpp"
#include <condition_variable>
#include <mutex>
#include <thread>

class SubtitleStream {
public:
    struct State { std::shared_ptr<const subtitles::Window> window;bool loading=false;std::string error; };
    SubtitleStream(MediaClient client,Playback playback);
    ~SubtitleStream();
    void select(int track);
    void position(int64_t ticks);
    State state();
private:
    MediaClient client_;Playback playback_;
    std::mutex mutex_;std::condition_variable wake_;bool stop_=false,loading_=false;
    int track_=-1;uint64_t generation_=0;int64_t position_=0;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::vector<std::shared_ptr<const subtitles::Window>> windows_;
    std::string error_;std::thread worker_;
    void run();
};
