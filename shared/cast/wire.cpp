#include "internal.hpp"
#include "tinyxml2.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>
namespace casting {
int64_t now_ms(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Fd::~Fd(){reset();}void Fd::reset(int v){if(n>=0)close(n);n=v;}
Url parse_url(const std::string&s){
    if(s.size()>8192||s.find_first_of("\r\n\t ")!=std::string::npos||s.find('\0')!=std::string::npos)throw std::runtime_error("无效媒体地址");
    auto i=s.find("://");if(i==std::string::npos)throw std::runtime_error("地址缺少协议");Url u;u.scheme=s.substr(0,i);if(u.scheme!="http"&&u.scheme!="https")throw std::runtime_error("仅支持 HTTP/HTTPS");
    size_t start=i+3,end=s.find_first_of("/?#",start);std::string authority=s.substr(start,end==std::string::npos?end:end-start);
    if(authority.empty()||authority.find_first_of("@[]\\")!=std::string::npos)throw std::runtime_error("不支持此地址格式");
    u.port=u.scheme=="https"?443:80;auto c=authority.find(':');u.host=authority.substr(0,c);
    if(c!=std::string::npos){auto p=authority.substr(c+1);if(p.empty()||p.size()>5||p.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("无效端口");u.port=std::stoi(p);if(u.port<1||u.port>65535)throw std::runtime_error("无效端口");}
    if(u.host.empty()||u.host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_")!=std::string::npos)throw std::runtime_error("无效主机名");
    u.path=end==std::string::npos?"/":s.substr(end);auto hash=u.path.find('#');if(hash!=std::string::npos)u.path.resize(hash);if(u.path.empty()||u.path[0]!='/')u.path="/"+u.path;return u;
}
bool media_url(const std::string&s){try{parse_url(s);return true;}catch(...){return false;}}
void wait_fd(int fd,short ev,int64_t end){for(;;){auto left=end-now_ms();if(left<=0)throw std::runtime_error("投屏连接超时");pollfd p{fd,ev,0};int r=::poll(&p,1,int(left));if(r<0&&errno==EINTR)continue;if(r<=0)throw std::runtime_error("投屏连接超时");if(p.revents&ev)return;throw std::runtime_error("投屏连接已关闭");}}
int tcp(const std::string&host,int port,int timeout){
    addrinfo hint{};hint.ai_socktype=SOCK_STREAM;hint.ai_family=AF_INET;addrinfo *raw=nullptr;auto service=std::to_string(port);
    if(getaddrinfo(host.c_str(),service.c_str(),&hint,&raw))throw std::runtime_error("无法解析接收设备");std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> list(raw,freeaddrinfo);
    for(auto a=raw;a;a=a->ai_next){Fd fd(socket(a->ai_family,SOCK_STREAM,0));if(fd.n<0)continue;fcntl(fd.n,F_SETFD,FD_CLOEXEC);fcntl(fd.n,F_SETFL,O_NONBLOCK);
        if(::connect(fd.n,a->ai_addr,a->ai_addrlen)&&errno!=EINPROGRESS)continue;
        try{wait_fd(fd.n,POLLOUT,now_ms()+timeout);int e=0;socklen_t n=sizeof e;if(getsockopt(fd.n,SOL_SOCKET,SO_ERROR,&e,&n)||e)continue;return fd.release();}catch(...){}
    }throw std::runtime_error("无法连接投屏设备");
}
void send_all(int fd,const void*data,size_t n,int64_t end){auto p=(const char*)data;while(n){ssize_t r=send(fd,p,n,MSG_NOSIGNAL);if(r>0){p+=r;n-=r;}else if(r<0&&(errno==EAGAIN||errno==EINTR)){wait_fd(fd,POLLOUT,end);}else throw std::runtime_error("投屏发送失败");}}
Http http(const std::string&url,const std::string&method,const std::string&body,const std::vector<std::string>&headers,int timeout_ms){
    auto u=parse_url(url);if(u.scheme!="http")throw std::runtime_error("设备控制地址需要 HTTP");Fd fd(tcp(u.host,u.port));auto deadline=now_ms()+std::clamp(timeout_ms,100,15000);
    std::string req=method+" "+u.path+" HTTP/1.1\r\nHost: "+u.host+":"+std::to_string(u.port)+"\r\nConnection: close\r\nContent-Length: "+std::to_string(body.size())+"\r\n";
    for(auto&h:headers){if(h.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("无效 HTTP 头");req+=h+"\r\n";}req+="\r\n"+body;send_all(fd.n,req.data(),req.size(),deadline);
    std::string data;size_t split=std::string::npos,expected=0;bool has_length=false,chunked=false;char b[4096];
    while(data.size()<512*1024){wait_fd(fd.n,POLLIN,deadline);auto n=recv(fd.n,b,sizeof b,0);if(!n)break;if(n<0){if(errno==EINTR||errno==EAGAIN)continue;throw std::runtime_error("读取接收设备失败");}data.append(b,n);
        if(split==std::string::npos&&(split=data.find("\r\n\r\n"))!=std::string::npos){std::string h=data.substr(0,split);std::transform(h.begin(),h.end(),h.begin(),[](unsigned char c){return std::tolower(c);});
            auto p=h.find("\r\ncontent-length:");if(p!=std::string::npos){char*e=nullptr;unsigned long v=strtoul(h.c_str()+p+17,&e,10);if(e==h.c_str()+p+17||v>512*1024)throw std::runtime_error("设备响应过大");expected=v;has_length=true;}chunked=h.find("transfer-encoding: chunked")!=std::string::npos;
        }if(split!=std::string::npos&&has_length&&data.size()>=split+4+expected)break;
    }
    if(split==std::string::npos||data.size()>=512*1024)throw std::runtime_error("无效设备响应");Http out;try{out.code=std::stoi(data.substr(data.find(' ')+1,3));}catch(...){throw std::runtime_error("无效 HTTP 状态");}out.body=data.substr(split+4);
    if(chunked){std::string plain;size_t pos=0;while(pos<out.body.size()){auto e=out.body.find("\r\n",pos);if(e==std::string::npos)throw std::runtime_error("截断的响应");auto s=out.body.substr(pos,e-pos);char*end=nullptr;unsigned long n=strtoul(s.c_str(),&end,16);if(end==s.c_str()||(*end&&*end!=';'))throw std::runtime_error("无效响应分块");pos=e+2;if(!n){out.body=plain;return out;}if(n>out.body.size()-pos||out.body.size()-pos-n<2||out.body.compare(pos+n,2,"\r\n"))throw std::runtime_error("截断的响应分块");plain.append(out.body,pos,n);pos+=n+2;}throw std::runtime_error("缺少结束分块");}
    if(has_length){if(out.body.size()<expected)throw std::runtime_error("截断的响应");out.body.resize(expected);}return out;
}
std::string xml_escape(const std::string&s){std::string o;for(char c:s){switch(c){case '&':o+="&amp;";break;case '<':o+="&lt;";break;case '>':o+="&gt;";break;case '"':o+="&quot;";break;case '\'':o+="&apos;";break;default:o+=c;}}return o;}
std::string xml_text(const std::string&s,const std::string&name){tinyxml2::XMLDocument doc;if(doc.Parse(s.data(),s.size()))return {};std::vector<tinyxml2::XMLElement*> stack;for(auto p=doc.FirstChildElement();p;p=p->NextSiblingElement())stack.push_back(p);while(!stack.empty()){auto e=stack.back();stack.pop_back();std::string n=e->Name();auto colon=n.find(':');if(colon!=std::string::npos)n=n.substr(colon+1);if(n==name)return e->GetText()?e->GetText():"";for(auto p=e->FirstChildElement();p;p=p->NextSiblingElement())stack.push_back(p);}return {};}
static void var(std::vector<uint8_t>&o,uint64_t v){do{uint8_t b=v&127;v>>=7;o.push_back(b|(v?128:0));}while(v);}
static void field(std::vector<uint8_t>&o,int n,const std::string&s){var(o,(n<<3)|2);var(o,s.size());o.insert(o.end(),s.begin(),s.end());}
std::vector<uint8_t> cast_packet(const std::string&source,const std::string&dest,const std::string&ns,const Json&p){std::vector<uint8_t> o{8,0};field(o,2,source);field(o,3,dest);field(o,4,ns);o.push_back(40);o.push_back(0);field(o,6,p.dump());return o;}
static uint64_t getvar(const std::vector<uint8_t>&b,size_t&i){uint64_t v=0;for(int shift=0;shift<64;shift+=7){if(i==b.size())break;uint8_t c=b[i++];if(shift==63&&(c&254))break;v|=uint64_t(c&127)<<shift;if(!(c&128))return v;}throw std::runtime_error("无效 Cast 消息");}
Json cast_decode(const std::vector<uint8_t>&b){Json j=Json::object();size_t i=0;while(i<b.size()){auto tag=getvar(b,i);auto kind=tag&7,field=tag>>3;if(!field)throw std::runtime_error("无效 Cast 字段");if(kind==0){getvar(b,i);continue;}if(kind==1||kind==5){size_t n=kind==1?8:4;if(n>b.size()-i)throw std::runtime_error("截断 Cast 字段");i+=n;continue;}if(kind!=2)throw std::runtime_error("未知 Cast 字段");auto n=getvar(b,i);if(n>b.size()-i)throw std::runtime_error("截断 Cast 消息");std::string s((const char*)b.data()+i,n);i+=n;if(field==2)j["source"]=s;else if(field==3)j["destination"]=s;else if(field==4)j["namespace"]=s;else if(field==6){auto p=Json::parse(s,nullptr,false);if(p.is_discarded())throw std::runtime_error("无效 Cast JSON");j["payload"]=p;}}return j;}
std::unique_ptr<Receiver> receiver(const std::string&p){if(p=="cast")return cast_receiver();if(p=="dlna")return dlna_receiver();throw std::runtime_error("此接收协议尚未通过适配");}
}
