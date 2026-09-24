#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hls {
struct Segment { std::string uri; int64_t start=0,duration=0; };
struct Playlist { std::vector<Segment> segments; std::string variant; bool ended=false; };
inline std::string resolve(const std::string&base,const std::string&relative){
    if(relative.empty()||relative.find_first_of("\r\n\t ")!=std::string::npos||relative.rfind("//",0)==0)
        throw std::runtime_error("Invalid segment URL");
    if(relative.find("://")!=std::string::npos)return relative;
    auto scheme=base.find("://"),path=base.find('/',scheme==std::string::npos?0:scheme+3);
    if(scheme==std::string::npos)throw std::runtime_error("Invalid playlist URL");
    auto origin=base.substr(0,path);auto clean=base.substr(0,base.find_first_of("?#"));
    std::string joined=relative[0]=='/'?relative:clean.substr(origin.size(),clean.rfind('/')-origin.size()+1)+relative;
    auto query=joined.find('?');auto suffix=query==std::string::npos?"":joined.substr(query);joined=joined.substr(0,query);
    std::istringstream in(joined);std::string part;std::vector<std::string> parts;
    while(std::getline(in,part,'/')){if(part.empty()||part==".")continue;if(part==".."){if(parts.empty())throw std::runtime_error("Invalid segment path");parts.pop_back();}else parts.push_back(part);}
    for(auto&p:parts)origin+="/"+p;return origin+suffix;
}
inline Playlist parse(const std::string&body){
    if(body.size()>1048576||body.rfind("#EXTM3U",0)!=0)throw std::runtime_error("Invalid HLS playlist");
    Playlist out;std::istringstream in(body);std::string line;int64_t pending=0,total=0;bool variant=false;
    while(std::getline(in,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        if(line.rfind("#EXT-X-KEY:",0)==0&&line!="#EXT-X-KEY:METHOD=NONE")throw std::runtime_error("Encrypted HLS is unsupported");
        if(line.rfind("#EXT-X-MAP:",0)==0||line=="#EXT-X-DISCONTINUITY")throw std::runtime_error("HLS requires continuous MPEG-TS");
        if(line=="#EXT-X-ENDLIST")out.ended=true;
        if(line.rfind("#EXT-X-STREAM-INF:",0)==0)variant=true;
        if(line.rfind("#EXTINF:",0)==0){
            size_t consumed=0;auto value=line.substr(8);double seconds=std::stod(value,&consumed);
            if(!std::isfinite(seconds)||seconds<=0||seconds>15||(consumed<value.size()&&value[consumed]!=','))throw std::runtime_error("Invalid HLS segment duration");
            pending=int64_t(std::llround(seconds*10000000));
        }
        if(line[0]!='#'){
            if(variant){if(out.variant.empty())out.variant=line;variant=false;}
            else {if(pending<=0||out.segments.size()>=20000)throw std::runtime_error("Invalid HLS segment list");out.segments.push_back({line,total,pending});total+=pending;pending=0;}
        }
    }
    if(pending||(out.segments.empty()&&out.variant.empty()))throw std::runtime_error("Empty HLS playlist");
    return out;
}
inline size_t containing(const Playlist&p,int64_t time){
    size_t i=0;while(i+1<p.segments.size()&&p.segments[i+1].start<=time)++i;return i;
}
inline bool transport_stream(const std::string&data){
    if(data.empty()||data.size()%188)return false;
    for(size_t i=0;i<data.size();i+=188)if((unsigned char)data[i]!=0x47)return false;
    return true;
}
}
