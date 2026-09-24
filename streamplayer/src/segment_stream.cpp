#include "segment_stream.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

SegmentStream::SegmentStream(MediaClient c,Playback p,std::string fifo):client_(std::move(c)),playback_(std::move(p)),fifo_(std::move(fifo)),requested_(playback_.options){
    position_=playback_.start;sessions_.push_back(playback_);thread_=std::thread([this]{run();});
}
SegmentStream::~SegmentStream(){stop();}
void SegmentStream::request(StreamOptions options){std::lock_guard<std::mutex> lock(mutex_);requested_=options;state_.notice.clear();}
SegmentStream::State SegmentStream::state(){std::lock_guard<std::mutex> lock(mutex_);return state_;}
std::vector<Playback> SegmentStream::stop(){cancel_=true;if(thread_.joinable())thread_.join();return sessions_;}
std::string SegmentStream::fetch(const std::string&url){
    // Short-lived wget requests can see a transient DNS/connection failure.
    // No bytes reach the decoder until one complete response succeeds.
    for(int attempt=0;;++attempt){try{return client_.fetch(url,&cancel_);}catch(...){
        if(cancel_||attempt==1)throw;
        std::fprintf(stderr,"[video] retrying incomplete media request\n");
        for(int i=0;i<10&&!cancel_;++i)usleep(20000);
    }}
}
std::pair<hls::Playlist,std::string> SegmentStream::playlist(const Playback&p,const StreamOptions&o){
    auto url=client_.hls_url(p,o,p.session);
    for(int depth=0;depth<3;++depth){
        auto list=hls::parse(fetch(url));
        if(!list.segments.empty()){
            if(!list.ended)throw std::runtime_error("HLS live playlists are not supported yet");
            return {std::move(list),url};
        }
        url=hls::resolve(url,list.variant);
    }
    throw std::runtime_error("Too many nested HLS playlists");
}
void SegmentStream::run(){
    int fd=-1;
    try{
        auto active=playback_;auto options=active.options;auto list=playlist(active,options);
        size_t index=hls::containing(list.first,playback_.start);unsigned revision=0;
        {std::lock_guard<std::mutex> lock(mutex_);state_.start=list.first.segments[index].start;state_.ready=true;state_.changes.push_back({state_.start,options});}
        while(!cancel_&&fd<0){
            fd=open(fifo_.c_str(),O_WRONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);
            if(fd<0&&errno!=ENXIO&&errno!=EINTR)throw std::runtime_error("Cannot open segment pipe");
            if(fd<0)usleep(10000);
        }
        if(cancel_)throw std::runtime_error("Canceled");
        while(!cancel_&&index<list.first.segments.size()){
            // Bound look-ahead even when a decoder aggressively fills its cache.
            while(!cancel_){int64_t position;{std::lock_guard<std::mutex> lock(mutex_);position=position_;}
                if(list.first.segments[index].start<=position+60000000)break;usleep(20000);}
            if(cancel_)break;
            StreamOptions wanted;{std::lock_guard<std::mutex> lock(mutex_);wanted=requested_;}
            std::string data;int64_t boundary=list.first.segments[index].start;
            if(wanted!=options&&sessions_.size()>=32){
                std::lock_guard<std::mutex> lock(mutex_);state_.notice="Please stop and retry; server sessions could not be closed";requested_=wanted=options;
            }
            if(wanted!=options){
                Playback candidate=playback_;candidate.options=wanted;candidate.session=playback_.session+"-c1-"+std::to_string(++revision);
                sessions_.push_back(candidate);
                try{
                    auto replacement=playlist(candidate,wanted);auto next=hls::containing(replacement.first,boundary);
                    if(std::llabs(replacement.first.segments[next].start-boundary)>500000)
                        throw std::runtime_error("Transcode segment clocks do not align");
                    data=fetch(hls::resolve(replacement.second,replacement.first.segments[next].uri));
                    if(!hls::transport_stream(data))throw std::runtime_error("Invalid replacement TS segment");
                    // Coalesce rapid clicks before committing any replacement bytes.
                    bool superseded;{std::lock_guard<std::mutex> lock(mutex_);superseded=requested_!=wanted;}
                    if(superseded){try{client_.stop_transcode(candidate,&cancel_);sessions_.pop_back();}catch(...){}continue;}
                    auto previous=active;active=candidate;options=wanted;list=std::move(replacement);index=next;
                    {std::lock_guard<std::mutex> lock(mutex_);state_.changes.push_back({boundary,options});
                        if(state_.changes.size()>32)state_.changes.erase(state_.changes.begin());state_.notice.clear();}
                    std::fprintf(stderr,"[video] segment switch at_ms=%lld mode=%s max=%dx%d subtitle=%d\n",(long long)(boundary/10000),options.width_fill?"Width":"Fit",options.max_width(),options.max_height(),options.subtitle);
                    // All old bytes are already local. Release its server encoder
                    // even for the initial rendition; session reporting is separate.
                    try{client_.stop_transcode(previous,&cancel_);sessions_.erase(std::remove_if(sessions_.begin(),sessions_.end(),[&](const Playback&p){return p.session==previous.session;}),sessions_.end());}catch(...){}
                }catch(const std::exception&){
                    if(cancel_)break;
                    try{client_.stop_transcode(candidate,&cancel_);sessions_.pop_back();}catch(...){}
                    std::lock_guard<std::mutex> lock(mutex_);state_.notice="Switch failed; continuing previous settings";requested_=options;data.clear();
                }
            }
            if(cancel_)break;
            if(data.empty())data=fetch(hls::resolve(list.second,list.first.segments[index].uri));
            if(!hls::transport_stream(data))throw std::runtime_error("Invalid MPEG-TS segment");
            for(size_t offset=0;offset<data.size()&&!cancel_;){
                ssize_t n=write(fd,data.data()+offset,data.size()-offset);
                if(n>0){offset+=size_t(n);continue;}
                if(n<0&&(errno==EAGAIN||errno==EINTR)){pollfd out{fd,POLLOUT,0};poll(&out,1,50);continue;}
                throw std::runtime_error("Video decoder closed the segment pipe");
            }
            ++index;
        }
    }catch(const std::exception&e){if(!cancel_){std::lock_guard<std::mutex> lock(mutex_);state_.error=e.what();}}
    if(fd>=0)close(fd);
    std::lock_guard<std::mutex> lock(mutex_);state_.done=true;
}
