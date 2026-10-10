#include "client.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){assert(argc==2);MediaClient c;c.config={{"base",argv[1]},{"token","fixture + token"},{"user_id","listener"}};
    auto libs=c.libraries();assert(libs.size()==2&&libs[0]["CollectionType"]=="music");
    auto music=c.items(libs[0]["Id"],3,true,128);assert(music["Items"].size()==1&&music["Items"][0]["Type"]=="Audio"&&music["Items"][0]["Album"]=="Original music"&&music["Items"][0]["AlbumArtist"]=="Fixture");
    c.items(libs[1]["Id"],0,false); // preserve the existing video filter/default page size
    c.items("中文 / &",6,false,3,"北京 & 海");
    c.items("music",3,true,128,"音乐");
    c.items("music",0,true,3,"完全不存在");
    bool invalid=false;try{c.items("music",0,true,3,std::string(513,'x'));}catch(...){invalid=true;}assert(invalid);
    auto url=c.audio_url("track /&");assert(url.find("/Audio/track%20%2F%26/stream.mp3?")!=std::string::npos);
    assert(url.find("api_key=fixture%20%2B%20token")!=std::string::npos&&url.find("AudioCodec=mp3")!=std::string::npos&&url.find("StartTimeTicks=0")!=std::string::npos);
    auto r=c1::http("GET",url);assert(r.status==200&&r.body=="ID3fixture");
    bool threw=false;try{c.audio_url("");}catch(...){threw=true;}assert(threw);
    StreamOptions local;auto small=c.playback("movie",0,local);
    assert(small.url.find("maxwidth=400")!=std::string::npos&&small.url.find("maxheight=288")!=std::string::npos);
    StreamOptions tv;tv.television=true;tv.subtitle=2;auto large=c.playback("movie",0,tv);
    assert(large.hls_url.find("/master.m3u8?")!=std::string::npos&&large.hls_url.find("maxwidth=1280")!=std::string::npos);
    assert(large.hls_url.find("maxheight=720")!=std::string::npos&&large.hls_url.find("subtitlemethod=Encode")!=std::string::npos);
    assert(large.hls_url.find("subtitlestreamindex=2")!=std::string::npos&&large.hls_url.find("api_key=fixture%20%2B%20token")!=std::string::npos);
    assert(std::string(small.device_id())=="c1max-streamplayer"&&std::string(large.device_id())=="c1max-streamplayer-tv");
    assert(small.session!=large.session);
    assert(large.hls_url.find("deviceid=c1max-streamplayer-tv")!=std::string::npos);
    c.report(small,"",120000000);c.report(large,"Progress",130000000,true);
    c.stop_transcode(small);c.stop_transcode(large); // Exact session+device, never stop both outputs.
    c.stop_transcode(Playback{}); // Missing session must never become a wildcard stop.
    assert(large.subtitles.size()==1&&large.subtitles[0].index==2&&large.duration==600000000);
    assert(local.max_width()==400&&local.max_height()==288);local.width_fill=false;assert(local.max_height()==170);
    puts("PASS music API: music/video libraries, bounded tracks, metadata, authenticated MP3 URL and encoded IDs");
}
