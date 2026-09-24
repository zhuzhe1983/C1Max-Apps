// Emby/Jellyfin API flow ported from CardputerZero StreamPlayer's MediaServerClient.
// C1 Max uses bounded JSON requests, low-resolution transcodes and stock MPlayer.
#include "client.hpp"
#include "default_servers.hpp"
#include <stdexcept>
#include <unistd.h>
#include <filesystem>
void MediaClient::load(){
    try{config=Json::parse(c1::read_file(c1::data()+"/streamplayer/config.json",16384));
        if(!config.is_object())throw std::runtime_error("Invalid configuration");
        for(auto k:{"base","username","type","token","user_id"})if(config.contains(k)&&!config[k].is_string())throw std::runtime_error("Invalid configuration");
        c1::origin(config.at("base"));
    }catch(...){config=c1::default_server("streamplayer");}
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
        for(auto key:{"Name","SeriesName","Overview","Type"}){auto value=item.contains(key)&&item[key].is_string()?item[key].get<std::string>():std::string();
            if(value.size()>512)value.resize(512);std::replace(value.begin(),value.end(),'\0',' ');row[key]=value;}
        for(auto key:{"ProductionYear","RunTimeTicks","CommunityRating"})if(item.contains(key)&&item[key].is_number())row[key]=item[key];
        if(row["Name"].get<std::string>().empty())row["Name"]="Untitled";out.push_back(row);
    }return out;
}
Json MediaClient::libraries(){return clean_items(call("GET","/Users/"+c1::encode(config.at("user_id"))+"/Views").at("Items"));}
Json MediaClient::items(const std::string&parent,int start){
    auto r=call("GET","/Users/"+c1::encode(config.at("user_id"))+"/Items?ParentId="+c1::encode(parent)+"&Recursive=true&IncludeItemTypes=Movie,Episode,Video&StartIndex="+std::to_string(start)+"&Limit=3&SortBy=SortName&SortOrder=Ascending&Fields=RunTimeTicks,SeriesName,Overview,ProductionYear,CommunityRating");
    auto items=clean_items(r.at("Items"));int total=r.contains("TotalRecordCount")&&r["TotalRecordCount"].is_number_integer()?r["TotalRecordCount"].get<int>():start+items.size();
    return {{"Items",items},{"TotalRecordCount",total}};
}
Playback MediaClient::playback(const std::string&id,int64_t start,StreamOptions options){
    // Bounded software decode; the display mode also determines the server size.
    // Never claim hardware decode from the presence of a /dev/video node alone.
    Json conditions=Json::array();
    for(auto pair:std::vector<std::pair<std::string,std::string>>{{"Width",std::to_string(options.max_width())},{"Height",std::to_string(options.max_height())},{"VideoFramerate","20"}})
        conditions.push_back({{"Condition","LessThanEqual"},{"Property",pair.first},{"Value",pair.second},{"IsRequired",true}});
    Json transcode={{"Container","ts"},{"Type","Video"},{"VideoCodec","h264"},{"AudioCodec","aac"},{"Protocol","http"},{"Context","Streaming"},{"MaxAudioChannels","2"},{"CopyTimestamps",false},{"MinSegments",2},{"BreakOnNonKeyFrames",false}};
    Json profile={{"Name","C1Max adaptive"},{"MaxStreamingBitrate",464000},{"DirectPlayProfiles",Json::array()},
        {"TranscodingProfiles",Json::array({transcode})},
        {"CodecProfiles",Json::array({{{"Type","Video"},{"Codec","h264"},{"Conditions",conditions}}})}};
    Json body={{"UserId",config.at("user_id")},{"MaxStreamingBitrate",464000},{"MaxAudioChannels",2},{"EnableDirectPlay",false},{"EnableDirectStream",false},{"EnableTranscoding",true},{"AllowVideoStreamCopy",false},{"AllowAudioStreamCopy",false},{"IsPlayback",true},{"DeviceProfile",profile}};
    std::string params="&VideoCodec=h264&AudioCodec=aac&VideoBitrate=400000&AudioBitrate=64000&MaxWidth=400&MaxHeight="+std::to_string(options.max_height())+"&MaxFramerate=20&Framerate=20&h264-profile=baseline&h264-level=21&MaxAudioChannels=2&AudioSampleRate=44100&Profile=baseline&Level=21&SegmentContainer=ts&SegmentLength=3&AllowVideoStreamCopy=false&AllowAudioStreamCopy=false";
    start=std::max<int64_t>(0,start);body["StartTimeTicks"]=start;body["SubtitleStreamIndex"]=-1;
    params+="&SubtitleStreamIndex=-1&SubtitleMethod=External";
    params+="&StartTimeTicks="+std::to_string(start)+"&CopyTimestamps=false";
    auto r=call("POST","/Items/"+c1::encode(id)+"/PlaybackInfo?UserId="+c1::encode(config.at("user_id"))+"&MaxStreamingBitrate=464000"+params,body);
    Playback p;p.options=options;p.item=id;p.start=start;p.session=r.value("PlaySessionId",std::string());
    if(p.session.empty())throw std::runtime_error("Missing playback session ID");
    for(auto&s:r.at("MediaSources")){
        if(!s.value("SupportsTranscoding",false))continue;
        p.source=s.at("Id");
        if(s.contains("MediaStreams")&&s["MediaStreams"].is_array())for(auto&t:s["MediaStreams"]){
            if(t.value("Type",std::string())!="Subtitle"||!t.contains("Index")||!t["Index"].is_number_integer())continue;
            SubtitleTrack track;track.index=t["Index"];track.codec=t.value("Codec",std::string());
            track.title=t.value("DisplayTitle",t.value("Language",std::string("Subtitle")));
            if(track.title.size()>256)track.title.resize(256);std::replace(track.title.begin(),track.title.end(),'\n',' ');
            p.subtitles.push_back(track);if(p.subtitles.size()==64)break;
        }
        if(s.contains("RunTimeTicks")&&s["RunTimeTicks"].is_number_integer())p.duration=std::max<int64_t>(0,s["RunTimeTicks"].get<int64_t>());
        // Stock MPlayer wraps HLS input in mp: URLs, breaking nested segments.
        // Both APIs expose a progressive MPEG-TS endpoint for the same transcode.
        std::string url=config.at("base").get<std::string>()+"/Videos/"+c1::encode(id)+"/stream.ts?MediaSourceId="+c1::encode(p.source)+"&PlaySessionId="+c1::encode(p.session)+"&Static=false"+params;
        // Normalize negotiated constraints instead of appending duplicate query keys.
        auto pos=url.find('?');std::string path=url.substr(0,pos);std::map<std::string,std::string> q;
        auto add=[&](std::string query){size_t off=0;while(off<query.size()){auto end=query.find('&',off);auto part=query.substr(off,end==std::string::npos?end:end-off);auto eq=part.find('=');auto key=part.substr(0,eq);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return std::tolower(c);});if(!key.empty())q[key]=(eq==std::string::npos?"":part.substr(eq+1));if(end==std::string::npos)break;off=end+1;}};
        if(pos!=std::string::npos)add(url.substr(pos+1));add(params.substr(1));q["api_key"]=c1::encode(config.at("token"));q["deviceid"]="c1max-streamplayer";
        url=path+"?";for(auto&kv:q)url+=kv.first+"="+kv.second+"&";url.pop_back();p.url=url;p.hls_url=hls_url(p,options,p.session);return p;
    }
    throw std::runtime_error("Server did not offer H.264/AAC transcoding");
}
std::string MediaClient::hls_url(const Playback&p,const StreamOptions&o,const std::string&session)const{
    // HLS segment timestamps stay on one source clock across renditions. Do not
    // reset PTS with StartTimeTicks: the feeder selects the matching segment.
    return config.at("base").get<std::string>()+"/Videos/"+c1::encode(p.item)+"/master.m3u8?"
        "MediaSourceId="+c1::encode(p.source)+"&PlaySessionId="+c1::encode(session)+
        "&DeviceId=c1max-streamplayer&VideoCodec=h264&AudioCodec=aac&VideoBitrate=400000&AudioBitrate=64000"
        "&MaxWidth=400&MaxHeight="+std::to_string(o.max_height())+
        "&MaxFramerate=20&Framerate=20&h264-profile=baseline&h264-level=21&Profile=baseline&Level=21"
        "&AudioSampleRate=44100&MaxAudioChannels=2&TranscodingMaxAudioChannels=2"
        "&SegmentContainer=ts&SegmentLength=3&MinSegments=1&CopyTimestamps=true"
        "&EnableAutoStreamCopy=false&AllowVideoStreamCopy=false&AllowAudioStreamCopy=false"
        "&SubtitleMethod=External&SubtitleStreamIndex=-1";
}
std::string MediaClient::subtitle(const Playback&p,int track,const std::string&format,int64_t start,int64_t end,const std::atomic<bool>*cancel){
    if(track<0||(format!="sup"&&format!="vtt")||start<0||end<=start)throw std::runtime_error("Invalid subtitle request");
    return fetch(config.at("base").get<std::string>()+"/Videos/"+c1::encode(p.item)+"/"+c1::encode(p.source)+"/Subtitles/"+std::to_string(track)+"/"+std::to_string(start)+"/Stream."+format+"?EndPositionTicks="+std::to_string(end)+"&CopyTimestamps=true",cancel);
}
std::string MediaClient::fetch(const std::string&url,const std::atomic<bool>*cancel){
    if(c1::origin(url)!=c1::origin(config.at("base")))throw std::runtime_error("Cross-server media URL refused");
    auto r=c1::http("GET",url,headers(),"",cancel);
    if(r.status!=200)throw std::runtime_error("Media server HTTP "+std::to_string(r.status));return r.body;
}
std::string MediaClient::poster(const std::string&id){
    auto dir=c1::data()+"/streamplayer/posters";std::filesystem::create_directories(dir);
    auto path=dir+"/"+c1::encode(id)+"-140x194.png";
    if(access(path.c_str(),R_OK)==0)return path;
    std::vector<std::filesystem::directory_entry> cached;
    for(auto&e:std::filesystem::directory_iterator(dir))if(e.is_regular_file()&&e.path().extension()==".png")cached.push_back(e);
    std::sort(cached.begin(),cached.end(),[](const auto&a,const auto&b){return a.last_write_time()<b.last_write_time();});
    // Three visible covers, at most 60 on disk; no full-library preload.
    while(cached.size()>=60){std::filesystem::remove(cached.front());cached.erase(cached.begin());}
    auto data=fetch(config.at("base").get<std::string>()+"/Items/"+c1::encode(id)+"/Images/Primary?Width=140&Height=194&Format=png&Quality=85");
    if(data.size()<24||data.compare(0,8,std::string("\x89PNG\r\n\x1a\n",8)))return "";
    auto be=[&](int off){uint32_t v=0;for(int i=0;i<4;i++)v=(v<<8)|(unsigned char)data[off+i];return v;};
    if(be(16)>256||be(20)>256)return "";c1::save_private(path,data);return path;
}
void MediaClient::report(const Playback&p,const std::string&e,int64_t ticks,bool paused){
    call("POST","/Sessions/Playing"+(e.empty()?"":"/"+e),{{"ItemId",p.item},{"MediaSourceId",p.source},{"PlaySessionId",p.session},{"PositionTicks",ticks},{"IsPaused",paused},{"CanSeek",p.duration>0},{"SubtitleStreamIndex",p.options.subtitle},{"PlayMethod","Transcode"}});
}
void MediaClient::stop_transcode(const Playback&p,const std::atomic<bool>*cancel){
    auto r=c1::http("DELETE",config.at("base").get<std::string>()+"/Videos/ActiveEncodings?DeviceId=c1max-streamplayer&PlaySessionId="+c1::encode(p.session),headers(),"",cancel);
    if(r.status<200||r.status>=300)throw std::runtime_error("Cannot stop server transcode");
}
