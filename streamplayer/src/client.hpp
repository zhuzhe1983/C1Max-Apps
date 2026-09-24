#pragma once
#include "net.hpp"
#include "stream_options.hpp"
struct Playback {std::string url,hls_url,item,source,session;int64_t duration=0,start=0;StreamOptions options;std::vector<SubtitleTrack> subtitles;};
class MediaClient {
public:
    Json config=Json::object();
    void load();
    void login(const std::string &base,const std::string &user,const std::string &password,const std::string&type);
    bool ready() const;
    Json libraries();
    Json items(const std::string &parent,int start=0);
    Playback playback(const std::string &id,int64_t start=0,StreamOptions options={});
    std::string hls_url(const Playback &p,const StreamOptions &options,const std::string &session) const;
    std::string fetch(const std::string &url,const std::atomic<bool> *cancel=nullptr);
    std::string subtitle(const Playback&,int track,const std::string&format,int64_t start,int64_t end,const std::atomic<bool>*cancel=nullptr);
    std::string poster(const std::string &id);
    void report(const Playback&p,const std::string&event,int64_t ticks,bool paused=false);
    void stop_transcode(const Playback&p,const std::atomic<bool>*cancel=nullptr);
private:
    std::vector<std::string> headers() const;
    Json call(const std::string &method,const std::string &path,const Json&body=nullptr);
};
