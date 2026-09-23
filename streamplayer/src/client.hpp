#pragma once
#include "net.hpp"
struct Playback {std::string url,item,source,session;int64_t duration=0,start=0;};
class MediaClient {
public:
    Json config=Json::object();
    void load();
    void login(const std::string &base,const std::string &user,const std::string &password,const std::string&type);
    bool ready() const;
    Json libraries();
    Json items(const std::string &parent,int start=0);
    Playback playback(const std::string &id,int64_t start=0);
    void report(const Playback&p,const std::string&event,int64_t ticks,bool paused=false);
    void stop_transcode(const Playback&p);
private:
    std::vector<std::string> headers() const;
    Json call(const std::string &method,const std::string &path,const Json&body=nullptr);
};
