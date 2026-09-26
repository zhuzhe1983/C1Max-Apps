#pragma once
#include "net.hpp"
#include <functional>
#include <map>
namespace bili {
using Params=std::map<std::string,std::string>;
std::string sign(Params params,const std::string&key,int64_t timestamp);
std::string mixin(const std::string&image,const std::string&sub);
std::string clean(const std::string&text,size_t limit=512);
std::string video_id(const std::string&input);
bool media_url(const std::string&url);
Json summary(const Json&video);
struct Stream {std::string url;int duration=0,quality=16;};
class Api {
public:
    using Transport=std::function<c1::Response(const std::string&,const std::vector<std::string>&)>;
    explicit Api(Transport transport={});
    Json cookies=Json::object();
    Json popular(int page);
    Json portrait(int page);
    Json search(const std::string&query,int page);
    Json detail(const std::string&bvid);
    Stream stream(const std::string&bvid,int64_t cid);
    Json account();
    Json qr();
    Json poll(const std::string&key);
    std::string poster(const Json&video);
private:
    Transport transport_;
    std::string key_;int64_t key_time_=0;
    Json request(const std::string&path,Params params={},bool signed_request=false,bool passport=false,bool raw=false);
};
}
