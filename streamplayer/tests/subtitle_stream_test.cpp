#include "subtitle_stream.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

static std::atomic<int> calls{0},active{0},canceled_calls{0};
static std::atomic<bool> slow{false};
// Isolated transport double: scheduling/cancellation tests never contact a server.
std::string MediaClient::subtitle(const Playback&,int track,const std::string&,int64_t,int64_t,const std::atomic<bool>*cancel){
    ++calls;++active;
    for(int i=0;slow&&i<500;++i){if(cancel&&*cancel){--active;++canceled_calls;throw std::runtime_error("Canceled");}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
    --active;return "WEBVTT\n\n00:01:50.000 --> 00:01:59.000\ntrack "+std::to_string(track)+"\n";
}
template<class F>static void until(F f){for(int n=0;n<1000;++n){if(f())return;std::this_thread::sleep_for(std::chrono::milliseconds(2));}assert(false&&"subtitle worker timeout");}
int main(){
    Playback p;p.start=1110000000;p.subtitles={{3,"Chinese","srt"},{4,"English","srt"},{5,"Other","srt"}};
    {
        SubtitleStream stream(MediaClient{},p);stream.select(3);
        until([&]{return bool(stream.state().window);});assert(subtitles::text_at(*stream.state().window,p.start)=="track 3");
        int fetched=calls;for(int n=0;n<20;++n)stream.position(p.start);std::this_thread::sleep_for(std::chrono::milliseconds(30));assert(calls==fetched);
        stream.position(1490000000);until([&]{auto s=stream.state();return s.window&&s.window->end>1600000000;});
        stream.position(1200000000);assert(stream.state().window); // cached earlier window
        stream.position(10000000000);until([&]{auto s=stream.state();return s.window&&s.window->start>9000000000;});
        slow=true;stream.select(4);until([]{return active.load()>0;});stream.select(5);until([]{return canceled_calls.load()>0;});slow=false;
        stream.position(p.start);until([&]{auto s=stream.state();return s.window&&subtitles::text_at(*s.window,p.start)=="track 5";});
        stream.select(-1);assert(!stream.state().window);assert(!stream.state().loading);
    }
    auto start=std::chrono::steady_clock::now();
    {slow=true;SubtitleStream stream(MediaClient{},p);stream.select(3);until([]{return active.load()>0;});}
    assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(1));assert(active==0);
    std::cout<<"subtitle worker: prefetch, backward/forward seek, track cancellation, Off and bounded exit PASS\n";
}
