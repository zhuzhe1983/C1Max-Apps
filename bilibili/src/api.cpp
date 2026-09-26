// SPDX-License-Identifier: GPL-3.0-or-later
// API flow and WBI permutation adapted from wiliwili; see licenses/NOTICE.md.
#include "api.hpp"
#include <mbedtls/md5.h>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <regex>
#include <set>
#include <stdexcept>
namespace bili {
static const char* ua="User-Agent: Mozilla/5.0";
static const char* referer="Referer: https://www.bilibili.com/";
static std::string str(const Json&j,const char*k,const std::string&fallback="") {return j.contains(k)&&j[k].is_string()?j[k].get<std::string>():fallback;}
static int64_t number(const Json&j,const char*k,int64_t fallback=0){return j.contains(k)&&j[k].is_number_integer()?j[k].get<int64_t>():fallback;}
std::string clean(const std::string&s,size_t limit){
    std::string out;bool tag=false;for(unsigned char c:s){if(c=='<'){tag=true;continue;}if(c=='>'){tag=false;continue;}if(!tag&&(c>=32||c=='\n'))out+=char(c);}
    for(auto pair:std::vector<std::pair<std::string,std::string>>{{"&amp;","&"},{"&quot;","\""},{"&#39;","'"},{"&lt;","<"},{"&gt;",">"},{"&nbsp;"," "}}){size_t p=0;while((p=out.find(pair.first,p))!=std::string::npos){out.replace(p,pair.first.size(),pair.second);p+=pair.second.size();}}
    if(out.size()>limit){size_t n=limit;while(n&&(static_cast<unsigned char>(out[n])&0xc0)==0x80)--n;out.resize(n);}return out;
}
std::string video_id(const std::string&s){std::smatch m;return std::regex_search(s,m,std::regex("BV[0-9A-Za-z]{10}(?![0-9A-Za-z])"))?m.str():"";}
bool media_url(const std::string&u){
    try{auto o=c1::origin(u);auto host=o.substr(o.find("://")+3);if(u.size()>8192||u.find_first_of("\\\r\n\t ")!=std::string::npos)return false;
        for(const std::string suffix:{".bilivideo.com",".bilivideo.cn",".hdslb.com",".bilibili.com"})if(host.size()>suffix.size()&&host.compare(host.size()-suffix.size(),suffix.size(),suffix)==0)return true;
    }catch(...){}return false;
}
std::string mixin(const std::string&i,const std::string&s){
    static const int order[]={46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,27,43,5,49,33,9,42,19,29,28,14,39,12,38,41,13};
    auto key=[](const std::string&u){auto at=u.rfind('/');auto part=u.substr(at==std::string::npos?0:at+1);return part.substr(0,part.find('.'));};
    auto raw=key(i)+key(s);if(raw.size()!=64)throw std::runtime_error("B 站签名响应无效");std::string out;for(int n:order)out+=raw[n];return out;
}
std::string sign(Params p,const std::string&key,int64_t timestamp){
    p.erase("w_rid");p["wts"]=std::to_string(timestamp);std::string query;
    for(auto&kv:p){auto v=kv.second;v.erase(std::remove_if(v.begin(),v.end(),[](char c){return std::string("!'()*").find(c)!=std::string::npos;}),v.end());if(!query.empty())query+='&';query+=c1::encode(kv.first)+"="+c1::encode(v);}
    auto input=query+key;unsigned char digest[16];if(mbedtls_md5_ret((const unsigned char*)input.data(),input.size(),digest))throw std::runtime_error("Signature failed");
    const char*hex="0123456789abcdef";query+="&w_rid=";for(auto c:digest){query+=hex[c>>4];query+=hex[c&15];}return query;
}
Json summary(const Json&v){
    auto id=video_id(str(v,"bvid"));if(id.empty())throw std::runtime_error("视频缺少 BV 号");
    auto owner=v.value("owner",Json::object());int64_t duration=number(v,"duration");
    if(!duration&&v.contains("duration")&&v["duration"].is_string()){std::string d=v["duration"];std::smatch m;if(std::regex_match(d,m,std::regex("([0-9]{1,4}):([0-9]{2})")))duration=std::stoi(m[1])*60+std::stoi(m[2]);}
    auto dim=v.value("dimension",Json::object());if(!dim.is_object())dim=Json::object();
    auto width=number(dim,"width",number(v,"width")),height=number(dim,"height",number(v,"height"));
    auto rotation=(number(dim,"rotate")%360+360)%360;if(rotation==90||rotation==270)std::swap(width,height);
    if(width<=0||height<=0||width>32768||height>32768)width=height=0;
    return {{"bvid",id},{"title",clean(str(v,"title","未命名视频"),256)},{"author",clean(str(owner,"name",str(v,"author")),128)},
        {"pic",str(v,"pic")},{"duration",std::clamp<int64_t>(duration,0,86400)},{"cid",number(v,"cid")},{"width",width},{"height",height}};
}
Api::Api(Transport t):transport_(std::move(t)){if(!transport_)transport_=[](auto&u,auto&h){return c1::http("GET",u,h);};}
Json Api::request(const std::string&path,Params params,bool signed_request,bool passport,bool raw){
    std::string query;if(signed_request){auto now=std::time(nullptr);if(key_.empty()||now-key_time_>3600){auto nav=request("/x/web-interface/nav",{},false,false,true);auto w=nav.at("data").at("wbi_img");key_=mixin(w.at("img_url"),w.at("sub_url"));key_time_=now;}query=sign(params,key_,now);}
    else for(auto&kv:params){if(!query.empty())query+='&';query+=c1::encode(kv.first)+"="+c1::encode(kv.second);}
    std::vector<std::string> h={ua,referer,"Accept: application/json"};std::string cookie;
    for(auto it=cookies.begin();it!=cookies.end();++it)if(it.value().is_string()&&std::regex_match(it.key(),std::regex("[A-Za-z0-9_]{1,32}"))){auto v=it.value().get<std::string>();if(v.size()<4096&&v.find_first_of(";\r\n") == std::string::npos){if(!cookie.empty())cookie+="; ";cookie+=it.key()+"="+v;}}
    if(!cookie.empty())h.push_back("Cookie: "+cookie);
    auto r=transport_((passport?"https://passport.bilibili.com":"https://api.bilibili.com")+path+(query.empty()?"":"?"+query),h);
    if(r.status!=200)throw std::runtime_error("B 站 HTTP "+std::to_string(r.status)+"，请稍后重试");
    auto j=Json::parse(r.body);if(!j.is_object())throw std::runtime_error("B 站响应无效");auto code=number(j,"code",-999);
    if(code!=0&&!raw)throw std::runtime_error("B 站 "+std::to_string(code)+" · "+clean(str(j,"message","请求失败"),160));
    // Only the passport API can update login cookies; CDN requests never receive them.
    if(passport&&path.find("/poll")!=std::string::npos&&code==0&&j.value("data",Json::object()).value("code",-1)==0){
        Json next=Json::object();for(auto&s:r.set_cookies){auto at=s.find('=');if(at==std::string::npos)continue;auto k=s.substr(0,at);auto v=s.substr(at+1,s.find(';',at+1)-at-1);if(k=="SESSDATA"||k=="bili_jct"||k=="DedeUserID"||k=="DedeUserID__ckMd5")next[k]=v;}
        if(next.contains("SESSDATA"))cookies=std::move(next);else throw std::runtime_error("扫码已确认，但登录凭据缺失；请重新登录");
    }
    return raw?j:j.value("data",Json::object());
}
Json Api::popular(int page){auto d=request("/x/web-interface/popular",{{"pn",std::to_string(std::clamp(page,1,100))},{"ps","20"}});Json list=Json::array();for(auto&v:d.at("list")){try{list.push_back(summary(v));}catch(...){}if(list.size()==20)break;}return {{"items",list},{"more",page<100&&!d.value("no_more",false)}};}
Json Api::portrait(int page){
    // A bounded local filter of public popular results, not an undocumented
    // phone recommendation feed. Unknown/square dimensions are not portrait.
    Json list=Json::array();std::set<std::string> seen;bool more=false;
    if(page<1||page>34)return {{"items",list},{"more",false}};
    for(int n=(page-1)*3+1;n<=std::min(page*3,100);++n){auto batch=popular(n);
        for(auto&v:batch["items"])if(v.value("width",0)>0&&v.value("height",0)>v.value("width",0)&&seen.insert(v["bvid"]).second)list.push_back(v);
        more=batch.value("more",false);if(!more)break;
    }
    return {{"items",list},{"more",more}};
}
Json Api::search(const std::string&q,int page){
    if(q.empty()||q.size()>160)throw std::runtime_error("请输入关键词或 BV 号");auto id=video_id(q);if(!id.empty())return {{"items",Json::array({summary(detail(id))})},{"more",false}};
    auto d=request("/x/web-interface/wbi/search/type",{{"keyword",q},{"search_type","video"},{"page",std::to_string(std::clamp(page,1,100))},{"page_size","20"},{"order","totalrank"}},true);
    Json list=Json::array();if(d.contains("result"))for(auto&v:d["result"]){try{list.push_back(summary(v));}catch(...){}if(list.size()==20)break;}
    return {{"items",list},{"more",page<number(d,"numPages")}};
}
Json Api::detail(const std::string&input){auto id=video_id(input);if(id.empty())throw std::runtime_error("BV 号格式无效");auto d=request("/x/web-interface/view",{{"bvid",id}});auto out=summary(d);out["owner"]={{"name",out["author"]}};out["desc"]=clean(str(d,"desc"),1024);out["pages"]=Json::array();
    for(auto&p:d.at("pages")){auto cid=number(p,"cid");if(cid>0)out["pages"].push_back({{"cid",cid},{"part",clean(str(p,"part","正片"),160)},{"duration",number(p,"duration")}});if(out["pages"].size()==256)break;}
    if(out["pages"].empty())throw std::runtime_error("没有可播放的分 P");return out;
}
Stream Api::stream(const std::string&id,int64_t cid){
    if(video_id(id)!=id||cid<=0)throw std::runtime_error("视频参数无效");auto d=request("/x/player/wbi/playurl",{{"bvid",id},{"cid",std::to_string(cid)},{"qn","16"},{"fnval","1"},{"fnver","0"},{"fourk","0"},{"platform","html5"}},true);
    if(!d.contains("durl")||!d["durl"].is_array()||d["durl"].size()!=1)throw std::runtime_error("此视频没有单文件 MP4 直播放源，首版暂不支持");
    auto u=str(d["durl"][0],"url");if(!media_url(u))throw std::runtime_error("视频源地址无效");
    if(number(d,"quality",999)>16)throw std::runtime_error("服务器没有返回 360p 低清视频");
    return {u,int(number(d,"timelength")/1000),int(number(d,"quality",16))};
}
Json Api::account(){auto j=request("/x/web-interface/nav",{},false,false,true);auto d=j.value("data",Json::object());return {{"logged_in",d.value("isLogin",false)},{"name",clean(str(d,"uname","游客"),64)}};}
Json Api::qr(){auto d=request("/x/passport-login/web/qrcode/generate",{},false,true);auto u=str(d,"url");if(u.rfind("https://",0)||u.size()>1024||str(d,"qrcode_key").size()>256)throw std::runtime_error("登录二维码响应无效");return {{"url",u},{"key",d.at("qrcode_key")}};}
Json Api::poll(const std::string&key){if(key.empty()||key.size()>256)throw std::runtime_error("二维码无效");auto d=request("/x/passport-login/web/qrcode/poll",{{"qrcode_key",key}},false,true);return {{"code",number(d,"code",-1)}};}
std::string Api::poster(const Json&v){
    auto id=video_id(str(v,"bvid"));auto u=str(v,"pic");if(u.rfind("//",0)==0)u="https:"+u;if(u.rfind("http://",0)==0)u="https://"+u.substr(7);if(id.empty()||!media_url(u))return "";
    auto dir=std::filesystem::path(c1::data()+"/bilibili/posters");std::filesystem::create_directories(dir);auto path=dir/(id+".png");if(std::filesystem::is_regular_file(path))return path;
    std::vector<std::filesystem::directory_entry> files;for(auto&e:std::filesystem::directory_iterator(dir))if(e.is_regular_file()&&e.path().extension()==".png")files.push_back(e);
    std::sort(files.begin(),files.end(),[](auto&a,auto&b){return a.last_write_time()<b.last_write_time();});while(files.size()>=60){std::filesystem::remove(files.front());files.erase(files.begin());}
    u+="@192w_108h_1c.png";auto r=transport_(u,{ua,referer});if(r.status!=200||r.body.size()<24||r.body.compare(0,8,std::string("\x89PNG\r\n\x1a\n",8)))return "";
    auto dim=[&](int pos){uint32_t n=0;for(int i=0;i<4;i++)n=n*256+(unsigned char)r.body[pos+i];return n;};if(dim(16)>256||dim(20)>256)return "";c1::save_private(path,r.body);return path;
}
}
