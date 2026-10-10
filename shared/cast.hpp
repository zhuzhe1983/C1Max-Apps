#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace casting {
using Json=nlohmann::json;
// Calls only the local daemon; network operations run outside the UI thread.
Json call(const Json &request, bool start=false);
Json status();
bool selected();
Json load(const std::string &owner,const std::string &url,const std::string &title,
          const std::string &mime,bool live=false,const std::string &image="",double position=0);
Json command(const std::string &owner,const std::string &action,double value=0);
void detach(const std::string &owner);
inline std::string audio_mime(std::string url) {
    for(auto &c:url)if(c>='A'&&c<='Z')c+=32;
    auto end=url.find('?');auto path=url.substr(0,end);
    auto ends=[&](const char *suffix){std::string s=suffix;return path.size()>=s.size()&&path.compare(path.size()-s.size(),s.size(),s)==0;};
    if(ends(".m3u8"))return "application/x-mpegURL";
    if(ends(".aac"))return "audio/aac";
    if(ends(".m4a")||ends(".mp4"))return "audio/mp4";
    if(ends(".ogg")||ends(".opus"))return "audio/ogg";
    if(ends(".flac"))return "audio/flac";
    if(ends(".wav"))return "audio/wav";
    return "audio/mpeg";
}
}
