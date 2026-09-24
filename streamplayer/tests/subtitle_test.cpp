#include "subtitles.hpp"
#include "../../shared/video_layout.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

static void word(std::string&s,unsigned n){s+=char(n>>8);s+=char(n);}
static void dword(std::string&s,unsigned n){word(s,n>>16);word(s,n);}
static std::string packet(unsigned type,unsigned pts,const std::string&data){std::string s="PG";dword(s,pts);dword(s,pts);s+=char(type);word(s,data.size());return s+data;}
static std::string pcs(unsigned pts,bool visible){
    std::string d;word(d,1920);word(d,1080);d+=char(0x10);word(d,1);d+=char(0x80);d+=char(0);d+=char(0);d+=char(visible);
    if(visible){word(d,0);d+=char(0);d+=char(0);word(d,300);word(d,900);}return packet(0x16,pts,d);
}
int main(int argc,char**argv){
    auto t=subtitles::vtt("WEBVTT\n\n1\n00:01:51.000 --> 00:01:53.250 align:middle\n<b>中文</b> &amp; text\nsecond line\n\n00:01:52.000 --> 00:01:54.000\noverlap\n",1000000000,1200000000);
    assert(t.cues.size()==2);assert(subtitles::text_at(t,1110000000)=="中文 & text\nsecond line");
    assert(subtitles::text_at(t,1120000000)=="中文 & text\nsecond line\noverlap");assert(subtitles::text_at(t,1140000000).empty());
    bool failed=false;try{subtitles::vtt("00:01:59.000 --> 00:01:61.000\nx\n",0,2000000000);}catch(...){failed=true;}assert(failed);
    std::string palette;palette+=char(0);palette+=char(0);palette+=char(1);palette+=char(235);palette+=char(128);palette+=char(128);palette+=char(255);
    std::string rle;for(int y=0;y<2;++y){rle+=char(1);rle+=char(1);rle+=char(0);rle+=char(0);}
    std::string object;word(object,0);object+=char(0);object+=char(0xc0);object+=char(0);word(object,rle.size()+4);word(object,2);word(object,2);object+=rle;
    auto sup=pcs(90000,true)+packet(0x14,90000,palette)+packet(0x15,90000,object)+packet(0x80,90000,"")+pcs(270000,false)+packet(0x80,270000,"");
    auto p=subtitles::pgs(sup,0,600000000);assert(p.cues.size()==1);assert(p.cues[0].start==10000000&&p.cues[0].end==30000000);
    assert(!subtitles::bitmap_at(p,9999999));assert(subtitles::bitmap_at(p,10000000));assert(!subtitles::bitmap_at(p,30000000));
    auto im=subtitles::bitmap(p.cues[0],32);assert(im.width==36&&im.height==36);for(auto pixel:im.pixels)assert(pixel==0xffffffff);
    auto small=subtitles::bitmap(p.cues[0],24);assert(small.height==28);
    // A new acquisition must not mutate a previously retained cue object.
    auto original=p.cues[0].objects[0].object->rle;assert(original==std::vector<uint8_t>(rle.begin(),rle.end()));
    failed=false;try{subtitles::pgs(sup.substr(0,sup.size()-1),0,600000000);}catch(...){failed=true;}assert(failed);
    auto broken=p.cues[0];auto o=std::make_shared<subtitles::Object>(*broken.objects[0].object);o->rle={0,0x83,1,0,0};o->expected=o->rle.size();broken.objects[0].object=o;
    failed=false;try{subtitles::bitmap(broken);}catch(...){failed=true;}assert(failed); // row overflow
    for(size_t n=0;n<sup.size();++n){try{auto truncated=subtitles::pgs(sup.substr(0,n),0,600000000);for(const auto&c:truncated.cues)subtitles::bitmap(c);}catch(const std::exception&){} }
    for(size_t n=0;n<sup.size();++n){auto damaged=sup;damaged[n]=char(255);try{auto parsed=subtitles::pgs(damaged,0,600000000);for(const auto&c:parsed.cues)subtitles::bitmap(c);}catch(const std::exception&){} }
    // Safe caption placement is identical in Width and Fit; controls lift it.
    VideoLayout layout;std::vector<uint32_t> rgb(320*180,0xff223344),ui(800*340,0xff123456),out(800*340),ink(4,0xffffffff);VideoCaption cap{ink.data(),2,2};
    auto pixel=[&](int x,int y){return out[size_t(799-x)*340+y];};
    for(bool fill:{false,true}){
        layout.configure(320,180,1,1,fill);layout.compose(reinterpret_cast<uint8_t*>(out.data()),1360,rgb.data(),320,ui.data(),false,false,&cap);
        assert(pixel(399,324)==0xffffffff&&pixel(400,325)==0xffffffff&&pixel(399,326)!=0xffffffff);
        layout.compose(reinterpret_cast<uint8_t*>(out.data()),1360,rgb.data(),320,ui.data(),true,false,&cap);assert(pixel(399,204)==0xffffffff&&pixel(399,324)==0xff123456);
        layout.compose(reinterpret_cast<uint8_t*>(out.data()),1360,rgb.data(),320,ui.data(),true,true,&cap);assert(pixel(399,204)==0xff123456);
    }
    // Optional real SUP export exercises palette, fragment and RLE boundaries.
    if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(f)),{});auto real=subtitles::pgs(bytes,30000000000,33000000000);size_t count=0;
        for(const auto&c:real.cues){auto image=subtitles::bitmap(c);assert(image.width<=752&&image.height<=112);if(!image.pixels.empty())++count;}
        assert(count>0);std::cout<<"Real PGS cues decoded: "<<count<<'\n';
    }
    std::cout<<"subtitles: VTT timing/overlap/markup, PGS palettes/RLE/clear/bounds, safe overlay in both modes PASS\n";
}
