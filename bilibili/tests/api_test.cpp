#include "api.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace bili;
int main(int argc,char**argv){
    if(argc==2&&std::string(argv[1])=="--live"){
        try{Api live;auto home=live.popular(1);std::cout<<"popular="<<home.at("items").size()<<std::endl;
            auto d=live.detail(home["items"][0]["bvid"]);auto stream=live.stream(d["bvid"],d["pages"][0]["cid"]);std::cout<<"pages="<<d["pages"].size()<<" quality="<<stream.quality<<std::endl;
            auto search=live.search("processing",1);std::cout<<"search="<<search.at("items").size()<<std::endl;
            auto vertical=live.portrait(1);for(auto&v:vertical["items"])assert(v["width"]>0&&v["height"]>v["width"]);std::cout<<"portrait="<<vertical["items"].size()<<std::endl;
            auto p=live.poster(home["items"][0]);std::cout<<"poster="<<(!p.empty())<<std::endl;
            auto qr=live.qr();auto state=live.poll(qr["key"]);std::cout<<"qr_generated=1 pending_code="<<state.at("code")<<std::endl;
            return 0;
        }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}
    }

    assert(video_id("https://www.bilibili.com/video/BV14Dho6WE66/?p=1")=="BV14Dho6WE66");
    assert(video_id("BV14Dho6WE66extra").empty());
    assert(clean("<em class='keyword'>视频</em>&amp;test") == "视频&test");
    assert(clean("一二三",4)=="一");
    assert(media_url("https://abc.bilivideo.com/test.mp4?a=1"));
    for(auto s:{"file:///etc/passwd","https://abc.bilivideo.com.evil.test/x","https://user@abc.bilivideo.com/x","https://127.0.0.1/x","https://abc.bilivideo.com/x\r\nHeader:1"})assert(!media_url(s));
    assert(sign({{"foo","a!b(c)*'"},{"bar","hello world"}},"test",1700000000)=="bar=hello%20world&foo=abc&wts=1700000000&w_rid=bbf7ed1a7181005cf63f307ce8915bfb");
    auto v=summary({{"bvid","BV14Dho6WE66"},{"title","<em>测试</em>"},{"duration","07:16"},{"author","UP主"}});assert(v["title"]=="测试"&&v["duration"]==436);
    assert(v["width"]==0&&v["height"]==0);
    v=summary({{"bvid","BV14Dho6WE66"},{"dimension",{{"width",1920},{"height",1080},{"rotate",90}}}});
    assert(v["width"]==1080&&v["height"]==1920&&summary(v)["width"]==1080);
    int batches=0;
    Api vertical([&](const std::string&u,auto&)->c1::Response{
        ++batches;assert(u.find("pn="+std::to_string(batches))!=std::string::npos);
        return {200,Json{{"code",0},{"data",{{"no_more",false},{"list",Json::array({
            {{"bvid","BV14Dho6WE66"},{"dimension",{{"width",1080},{"height",1920}}}},
            {{"bvid","BV1XLhd6tEZ3"},{"dimension",{{"width",1920},{"height",1080}}}},
            {{"bvid","BV1qdey6GE1x"},{"dimension",{{"width",1080},{"height",1080}}}},
            {{"bvid","BV1R5hH6cEje"}}
        })}}}}.dump()};
    });
    auto filtered=vertical.portrait(1);assert(batches==3&&filtered["items"].size()==1&&filtered["more"]==true);
    assert(vertical.portrait(35)["items"].empty()&&batches==3);
    int end_calls=0;Api end([&](const std::string&u,auto&)->c1::Response{++end_calls;assert(u.find("pn=100")!=std::string::npos);return {200,"{\"code\":0,\"data\":{\"list\":[],\"no_more\":false}}"};});
    assert(end.portrait(34)["more"]==false&&end_calls==1);
    const Json nav={{"code",-101},{"data",{{"isLogin",false},{"wbi_img",{{"img_url","https://i0.hdslb.com/bfs/wbi/0123456789abcdef0123456789abcdef.png"},{"sub_url","https://i0.hdslb.com/bfs/wbi/fedcba9876543210fedcba9876543210.png"}}}}}};
    int nav_requests=0;bool signed_seen=false;
    Api api([&](const std::string&url,const std::vector<std::string>&h)->c1::Response{
        if(url.find("/nav")!=std::string::npos){++nav_requests;return {200,nav.dump()};}
        if(url.find("playurl")!=std::string::npos){signed_seen=url.find("w_rid=")!=std::string::npos;return {200,Json{{"code",0},{"data",{{"quality",16},{"timelength",436000},{"durl",Json::array({{{"url","https://abc.bilivideo.com/video.mp4"}}})}}}}.dump()};}
        if(url.find("/poll")!=std::string::npos)return {200,"{\"code\":0,\"data\":{\"code\":0}}",{"SESSDATA=secret; Path=/; Secure","bili_jct=csrf; Path=/","not_allowed=no"}};
        if(url.find("popular")!=std::string::npos){bool seen=false;for(auto&x:h)if(x.find("Cookie: SESSDATA=secret") == 0)seen=true;assert(seen);return {200,"{\"code\":-352,\"message\":\"risk check\"}"};}
        throw std::runtime_error("Unexpected request");
    });
    auto s=api.stream("BV14Dho6WE66",42);assert(s.quality==16&&s.duration==436&&signed_seen);api.stream("BV14Dho6WE66",42);assert(nav_requests==1);
    assert(api.poll("key")["code"]==0);assert(api.cookies.size()==2&&api.cookies["SESSDATA"]=="secret");
    bool rejected=false;try{api.popular(1);}catch(const std::exception&e){rejected=std::string(e.what()).find("-352")!=std::string::npos;}assert(rejected);
    Api dash([&](auto&url,auto&)->c1::Response{if(url.find("/nav")!=std::string::npos)return {200,nav.dump()};return {200,"{\"code\":0,\"data\":{\"quality\":16,\"dash\":{}}}"};});
    rejected=false;try{dash.stream("BV14Dho6WE66",42);}catch(...){rejected=true;}assert(rejected);
    Api qr([](auto&,auto&)->c1::Response{return {200,"{\"code\":0,\"data\":{\"code\":0}}"};});rejected=false;try{qr.poll("key");}catch(...){rejected=true;}assert(rejected&&qr.cookies.empty());
    std::cout<<"PASS WBI, ID parsing, text bounds, CDN checks, cookie scope, errors and unsupported streams\n";
}
