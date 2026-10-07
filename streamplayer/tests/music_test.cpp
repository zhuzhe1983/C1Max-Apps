#include "client.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){assert(argc==2);MediaClient c;c.config={{"base",argv[1]},{"token","fixture + token"},{"user_id","listener"}};
    auto libs=c.libraries();assert(libs.size()==2&&libs[0]["CollectionType"]=="music");
    auto music=c.items(libs[0]["Id"],3,true,128);assert(music["Items"].size()==1&&music["Items"][0]["Type"]=="Audio"&&music["Items"][0]["Album"]=="Original music"&&music["Items"][0]["AlbumArtist"]=="Fixture");
    c.items(libs[1]["Id"],0,false); // preserve the existing video filter/default page size
    auto url=c.audio_url("track /&");assert(url.find("/Audio/track%20%2F%26/stream.mp3?")!=std::string::npos);
    assert(url.find("api_key=fixture%20%2B%20token")!=std::string::npos&&url.find("AudioCodec=mp3")!=std::string::npos&&url.find("StartTimeTicks=0")!=std::string::npos);
    auto r=c1::http("GET",url);assert(r.status==200&&r.body=="ID3fixture");
    bool threw=false;try{c.audio_url("");}catch(...){threw=true;}assert(threw);
    puts("PASS music API: music/video libraries, bounded tracks, metadata, authenticated MP3 URL and encoded IDs");
}
