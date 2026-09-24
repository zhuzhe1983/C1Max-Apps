#include "subtitle_stream.hpp"
#include <chrono>
#include <cstdio>

SubtitleStream::SubtitleStream(MediaClient c,Playback p):client_(std::move(c)),playback_(std::move(p)),position_(playback_.start),worker_([this]{run();}){}
SubtitleStream::~SubtitleStream(){
    {std::lock_guard<std::mutex> lock(mutex_);stop_=true;if(cancel_)*cancel_=true;}wake_.notify_all();worker_.join();
}
void SubtitleStream::select(int track){
    {std::lock_guard<std::mutex> lock(mutex_);track_=track;++generation_;windows_.clear();error_.clear();loading_=track>=0;if(cancel_)*cancel_=true;}wake_.notify_all();
}
void SubtitleStream::position(int64_t ticks){
    {std::lock_guard<std::mutex> lock(mutex_);position_=ticks;}wake_.notify_all();
}
SubtitleStream::State SubtitleStream::state(){
    std::lock_guard<std::mutex> lock(mutex_);State s;s.loading=loading_;s.error=error_;
    for(auto&w:windows_)if(w->start<=position_&&position_<w->end)s.window=w;
    return s;
}
void SubtitleStream::run(){
    using namespace std::chrono;
    auto retry=steady_clock::now();uint64_t previous=0;
    std::unique_lock<std::mutex> lock(mutex_);
    while(!stop_){
        if(generation_!=previous){previous=generation_;retry=steady_clock::now();}
        int64_t start=std::max<int64_t>(0,(position_/300000000)*300000000-150000000),end=start+600000000;
        bool covered=false;for(auto&w:windows_)if(w->start<=position_&&w->end>position_+200000000)covered=true;
        if(track_<0||covered||steady_clock::now()<retry){wake_.wait_for(lock,milliseconds(500));continue;}
        if(!windows_.empty()&&windows_.back()->end>position_&&windows_.back()->end-position_<=200000000){start=windows_.back()->end-300000000;end=start+600000000;}
        auto found=std::find_if(playback_.subtitles.begin(),playback_.subtitles.end(),[&](const SubtitleTrack&t){return t.index==track_;});
        if(found==playback_.subtitles.end()){error_="Subtitle track unavailable";loading_=false;retry=steady_clock::now()+seconds(30);continue;}
        auto track=*found;auto gen=generation_;auto canceled=std::make_shared<std::atomic<bool>>(false);cancel_=canceled;loading_=true;
        lock.unlock();std::shared_ptr<subtitles::Window> result;std::string error;
        try{
            std::string format=subtitles::pgs_codec(track.codec)?"sup":subtitles::text_codec(track.codec)?"vtt":"";
            if(format.empty())throw std::runtime_error("This subtitle format is not supported locally");
            std::string body;for(int attempt=0;;++attempt){try{body=client_.subtitle(playback_,track.index,format,start,end,canceled.get());break;}catch(...){if(*canceled||attempt)throw;}}
            if(!*canceled)result=std::make_shared<subtitles::Window>(format=="sup"?subtitles::pgs(body,start,end):subtitles::vtt(body,start,end));
        }catch(const std::exception&e){error=e.what();}
        lock.lock();if(stop_)break;if(gen!=generation_)continue;
        loading_=false;error_=error;
        if(result){windows_.push_back(result);while(windows_.size()>2)windows_.erase(windows_.begin());
            std::fprintf(stderr,"[subtitle] local track=%d format=%s cues=%zu range_ms=%lld..%lld\n",track.index,subtitles::pgs_codec(track.codec)?"PGS":"VTT",result->cues.size(),(long long)(start/10000),(long long)(end/10000));
        }else{retry=steady_clock::now()+seconds(15);std::fprintf(stderr,"[subtitle] local subtitle request failed\n");}
    }
}
