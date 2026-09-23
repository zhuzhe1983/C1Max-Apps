// Emby/Jellyfin API flow ported from CardputerZero StreamPlayer's MediaServerClient.
// C1 Max uses bounded JSON requests, low-resolution transcodes and stock MPlayer.
#include "client.hpp"
#include <stdexcept>
#include <unistd.h>
void MediaClient::load(){
    try{config=Json::parse(c1::read_file(c1::data()+"/streamplayer/config.json",16384));
        if(!config.is_object())throw std::runtime_error("Invalid configuration");
        for(auto k:{"base","username","type","token","user_id"})if(config.contains(k)&&!config[k].is_string())throw std::runtime_error("Invalid configuration");
        c1::origin(config.at("base"));
    }catch(...){config=Json::object();}
}
bool MediaClient::ready()const{return config.is_object()&&!config.value("token",std::string()).empty()&&!config.value("user_id",std::string()).empty();}
std::vector<std::string> MediaClient::headers()const{
    std::vector<std::string> h={"Content-Type: application/json","Accept: application/json","X-Emby-Authorization: MediaBrowser Client=\"C1Max StreamPlayer\", Device=\"C1Max\", DeviceId=\"c1max-streamplayer\", Version=\"0.1.0\""};
    auto token=config.value("token",std::string());if(!token.empty())h.push_back("X-Emby-Token: "+token);return h;
}
Json MediaClient::call(const std::string&m,const std::string&p,const Json&body){return c1::request(m,config.at("base").get<std::string>()+p,headers(),body);}
void MediaClient::login(const std::string&input,const std::string&user,const std::string&pw,const std::string&type){
    std::string base=input;while(!base.empty()&&base.back()=='/')base.pop_back();c1::origin(base);
    if(base.find_first_of("?#")!=std::string::npos)throw std::runtime_error("Server address must not contain a query or fragment");
    auto previous=config;config={{"base",base},{"username",user},{"type",type}};
    try{auto r=call("POST","/Users/AuthenticateByName",{{"Username",user},{"Pw",pw}});auto token=r.at("AccessToken").get<std::string>();auto id=r.at("User").at("Id").get<std::string>();if(token.empty()||id.empty())throw std::runtime_error("Login response missing token/user");config["token"]=token;config["user_id"]=id;c1::save_private(c1::data()+"/streamplayer/config.json",config.dump(2));}catch(...){config=previous;throw;}
}
static Json clean_items(const Json&j){
    if(!j.is_array()||j.size()>256)throw std::runtime_error("Invalid media list");
    Json out=Json::array();for(auto&item:j){
        if(!item.is_object()||!item.contains("Id")||!item["Id"].is_string())continue;
        Json row={{"Id",item["Id"]}};
        for(auto key:{"Name","SeriesName"}){auto value=item.contains(key)&&item[key].is_string()?item[key].get<std::string>():std::string();
            if(value.size()>512)value.resize(512);std::replace(value.begin(),value.end(),'\0',' ');row[key]=value;}
        if(row["Name"].get<std::string>().empty())row["Name"]="Untitled";out.push_back(row);
    }return out;
}
Json MediaClient::libraries(){return clean_items(call("GET","/Users/"+c1::encode(config.at("user_id"))+"/Views").at("Items"));}
Json MediaClient::items(const std::string&parent,int start){
    auto r=call("GET","/Users/"+c1::encode(config.at("user_id"))+"/Items?ParentId="+c1::encode(parent)+"&Recursive=true&IncludeItemTypes=Movie,Episode,Video&StartIndex="+std::to_string(start)+"&Limit=24&SortBy=SortName&SortOrder=Ascending&Fields=RunTimeTicks,SeriesName");
    auto items=clean_items(r.at("Items"));int total=r.contains("TotalRecordCount")&&r["TotalRecordCount"].is_number_integer()?r["TotalRecordCount"].get<int>():start+items.size();
    return {{"Items",items},{"TotalRecordCount",total}};
}
Playback MediaClient::playback(const std::string&id,int64_t start){
    // Baseline 400x170/20fps is the conservative software-decoder preset.
    // Never claim hardware decode from the presence of a /dev/video node alone.
    Json conditions=Json::array();
    for(auto pair:std::vector<std::pair<std::string,std::string>>{{"Width","400"},{"Height","170"},{"VideoFramerate","20"}})
        conditions.push_back({{"Condition","LessThanEqual"},{"Property",pair.first},{"Value",pair.second},{"IsRequired",true}});
    Json transcode={{"Container","ts"},{"Type","Video"},{"VideoCodec","h264"},{"AudioCodec","aac"},{"Protocol","http"},{"Context","Streaming"},{"MaxAudioChannels","2"},{"CopyTimestamps",false},{"MinSegments",2},{"BreakOnNonKeyFrames",false}};
    Json profile={{"Name","C1Max 400x170"},{"MaxStreamingBitrate",464000},{"DirectPlayProfiles",Json::array()},
        {"TranscodingProfiles",Json::array({transcode})},
        {"CodecProfiles",Json::array({{{"Type","Video"},{"Codec","h264"},{"Conditions",conditions}}})}};
    Json body={{"UserId",config.at("user_id")},{"MaxStreamingBitrate",464000},{"MaxAudioChannels",2},{"EnableDirectPlay",false},{"EnableDirectStream",false},{"EnableTranscoding",true},{"AllowVideoStreamCopy",false},{"AllowAudioStreamCopy",false},{"IsPlayback",true},{"DeviceProfile",profile}};
    std::string params="&VideoCodec=h264&AudioCodec=aac&VideoBitrate=400000&AudioBitrate=64000&MaxWidth=400&MaxHeight=170&MaxFramerate=20&MaxAudioChannels=2&AudioSampleRate=44100&Profile=baseline&Level=21&SegmentContainer=ts&SegmentLength=3&AllowVideoStreamCopy=false&AllowAudioStreamCopy=false";
    start=std::max<int64_t>(0,start);body["StartTimeTicks"]=start;
    params+="&StartTimeTicks="+std::to_string(start)+"&CopyTimestamps=false";
    auto r=call("POST","/Items/"+c1::encode(id)+"/PlaybackInfo?UserId="+c1::encode(config.at("user_id"))+"&MaxStreamingBitrate=464000"+params,body);
    Playback p;p.item=id;p.start=start;p.session=r.value("PlaySessionId",std::string());
    if(p.session.empty())throw std::runtime_error("Missing playback session ID");
    for(auto&s:r.at("MediaSources")){
        if(!s.value("SupportsTranscoding",false))continue;
        p.source=s.at("Id");
        if(s.contains("RunTimeTicks")&&s["RunTimeTicks"].is_number_integer())p.duration=std::max<int64_t>(0,s["RunTimeTicks"].get<int64_t>());
        // Stock MPlayer wraps HLS input in mp: URLs, breaking nested segments.
        // Both APIs expose a progressive MPEG-TS endpoint for the same transcode.
        std::string url=config.at("base").get<std::string>()+"/Videos/"+c1::encode(id)+"/stream.ts?MediaSourceId="+c1::encode(p.source)+"&PlaySessionId="+c1::encode(p.session)+"&Static=false"+params;
        // Normalize negotiated constraints instead of appending duplicate query keys.
        auto pos=url.find('?');std::string path=url.substr(0,pos);std::map<std::string,std::string> q;
        auto add=[&](std::string query){size_t off=0;while(off<query.size()){auto end=query.find('&',off);auto part=query.substr(off,end==std::string::npos?end:end-off);auto eq=part.find('=');auto key=part.substr(0,eq);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return std::tolower(c);});if(!key.empty())q[key]=(eq==std::string::npos?"":part.substr(eq+1));if(end==std::string::npos)break;off=end+1;}};
        if(pos!=std::string::npos)add(url.substr(pos+1));add(params.substr(1));q["api_key"]=c1::encode(config.at("token"));q["deviceid"]="c1max-streamplayer";
        url=path+"?";for(auto&kv:q)url+=kv.first+"="+kv.second+"&";url.pop_back();p.url=url;return p;
    }
    throw std::runtime_error("Server did not offer H.264/AAC transcoding");
}
void MediaClient::report(const Playback&p,const std::string&e,int64_t ticks,bool paused){
    call("POST","/Sessions/Playing"+(e.empty()?"":"/"+e),{{"ItemId",p.item},{"MediaSourceId",p.source},{"PlaySessionId",p.session},{"PositionTicks",ticks},{"IsPaused",paused},{"CanSeek",p.duration>0},{"PlayMethod","Transcode"}});
}
void MediaClient::stop_transcode(const Playback&p){call("DELETE","/Videos/ActiveEncodings?DeviceId=c1max-streamplayer&PlaySessionId="+c1::encode(p.session));}
