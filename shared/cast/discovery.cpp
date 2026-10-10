#include "internal.hpp"
#include "tinyxml2.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cstring>
#include <map>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <sys/socket.h>
namespace casting {
static uint16_t u16(const uint8_t*p){return (unsigned(p[0])<<8)|p[1];}
static std::string name(const uint8_t*b,size_t n,size_t&pos){std::string out;size_t at=pos,end=0;unsigned hops=0;for(;;){if(at>=n||++hops>128)throw std::runtime_error("DNS name bounds");uint8_t c=b[at++];if((c&192)==192){if(at>=n)throw std::runtime_error("DNS pointer bounds");if(!end)end=at+1;at=((c&63)<<8)|b[at];continue;}if(c&192)throw std::runtime_error("DNS label type");if(!c){pos=end?end:at;return out;}if(c>n-at||out.size()+c+1>1024)throw std::runtime_error("DNS label bounds");if(!out.empty())out+='.';out.append((const char*)b+at,c);at+=c;}}
std::vector<DnsRecord> dns_decode(const uint8_t*b,size_t n){
    if(n<12||n>9000)throw std::runtime_error("DNS size");unsigned questions=u16(b+4),records=u16(b+6)+u16(b+8)+u16(b+10);if(questions>64||records>256)throw std::runtime_error("DNS count");size_t i=12;std::vector<DnsRecord> out;
    for(unsigned k=0;k<questions;k++){name(b,n,i);if(i+4>n)throw std::runtime_error("DNS question");i+=4;}
    for(unsigned k=0;k<records;k++){DnsRecord r;r.name=name(b,n,i);if(i+10>n)throw std::runtime_error("DNS header");r.type=u16(b+i);size_t size=u16(b+i+8);i+=10;if(size>n-i)throw std::runtime_error("DNS data");size_t end=i+size,at=i;
        if(r.type==12)r.text=name(b,n,at);
        else if(r.type==33&&size>=6){r.port=u16(b+i+4);at=i+6;r.text=name(b,n,at);}
        else if(r.type==16){while(at<end){size_t len=b[at++];if(len>end-at)throw std::runtime_error("DNS TXT");r.text.append((const char*)b+at,len);r.text+='\n';at+=len;}}
        else if(r.type==1&&size==4){char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,b+i,ip,sizeof ip);r.text=ip;}
        if(!r.text.empty())out.push_back(std::move(r));i=end;
    }return out;
}
std::vector<uint8_t> dns_query(const std::string&s){std::vector<uint8_t> o(12,0);o[5]=1;size_t i=0;while(i<s.size()){auto p=s.find('.',i);auto n=(p==std::string::npos?s.size():p)-i;if(!n||n>63)throw std::runtime_error("DNS query");o.push_back(n);o.insert(o.end(),s.begin()+i,s.begin()+i+n);i+=n+1;}o.insert(o.end(),{0,0,12,128,1});return o;}
static void udp_send(int fd,const char*ip,int port,const void*data,size_t size){sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(port);inet_pton(AF_INET,ip,&a.sin_addr);sendto(fd,data,size,0,(sockaddr*)&a,sizeof a);}
static std::string txt(const std::string&s,const std::string&key){auto p=s.find(key+"=");if(p!=0&&(p==std::string::npos||s[p-1]!='\n'))return {};p+=key.size()+1;auto e=s.find('\n',p);return s.substr(p,e==std::string::npos?e:e-p);}
static std::string resolve_http(const std::string&base,const std::string&path){if(path.rfind("http://",0)==0)return path;auto u=parse_url(base);std::string root="http://"+u.host+":"+std::to_string(u.port);if(!path.empty()&&path[0]=='/')return root+path;return root+u.path.substr(0,u.path.rfind('/')+1)+path;}
std::vector<Json> discover(int milliseconds){
    Fd mdns(socket(AF_INET,SOCK_DGRAM,0)),ssdp(socket(AF_INET,SOCK_DGRAM,0));if(mdns.n<0||ssdp.n<0)throw std::runtime_error("无法打开设备搜索");
    for(auto s:{"_googlecast._tcp.local","_airplay._tcp.local","_raop._tcp.local"}){auto q=dns_query(s);udp_send(mdns.n,"224.0.0.251",5353,q.data(),q.size());}
    std::string search="M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\nST: urn:schemas-upnp-org:device:MediaRenderer:1\r\n\r\n";udp_send(ssdp.n,"239.255.255.250",1900,search.data(),search.size());
    std::map<std::string,DnsRecord> srvs;std::map<std::string,std::string> texts,addresses;std::set<std::string> locations;auto end=now_ms()+std::clamp(milliseconds,100,10000);
    while(now_ms()<end){pollfd f[2]={{mdns.n,POLLIN,0},{ssdp.n,POLLIN,0}};if(poll(f,2,int(end-now_ms()))<=0)continue;for(int k=0;k<2;k++)if(f[k].revents&POLLIN){uint8_t b[9000];sockaddr_in from{};socklen_t len=sizeof from;ssize_t n=recvfrom(f[k].fd,b,sizeof b,0,(sockaddr*)&from,&len);if(n<=0)continue;char ip[INET_ADDRSTRLEN];inet_ntop(AF_INET,&from.sin_addr,ip,sizeof ip);
        if(k==0){try{for(auto&r:dns_decode(b,n)){if(r.type==33){if(srvs.size()>=128&&!srvs.count(r.name))continue;srvs[r.name]=r;if(!addresses.count(r.text))addresses[r.text]=ip;}else if(r.type==16&&texts.size()<128)texts[r.name]=r.text;else if(r.type==1&&addresses.size()<256)addresses[r.name]=r.text;}}catch(...){}}
        else if(locations.size()<16){std::string s((const char*)b,n),lower=s;std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return std::tolower(c);});auto p=lower.find("\r\nlocation:");if(p!=std::string::npos){p+=11;while(p<s.size()&&s[p]==' ')p++;auto e=s.find('\r',p);auto url=s.substr(p,e-p);try{auto u=parse_url(url);if(u.scheme=="http"&&u.host==ip)locations.insert(url);}catch(...){}}}
    }}
    std::vector<Json> out;for(auto &[id,r]:srvs){if(out.size()>=32)break;std::string protocol;if(id.find("._googlecast._tcp.")!=std::string::npos)protocol="cast";else if(id.find("._airplay._tcp.")!=std::string::npos)protocol="airplay";else continue;
        auto host=addresses[r.text];if(host.empty()||!r.port)continue;auto title=txt(texts[id],"fn");if(title.empty())title=id.substr(0,id.find("._"));auto stable=txt(texts[id],"id");if(stable.empty())stable=id;
        out.push_back({{"id",protocol+":"+stable},{"name",title.substr(0,160)},{"protocol",protocol},{"host",host},{"port",r.port},{"supported",protocol=="cast"},{"detail",protocol=="cast"?"Google Cast":"AirPlay：需配对适配"}});
    }
    auto descriptions_end=now_ms()+5000;for(auto&location:locations){if(out.size()>=32||now_ms()>descriptions_end)break;try{auto result=http(location);if(result.code!=200)continue;tinyxml2::XMLDocument doc;if(doc.Parse(result.body.data(),result.body.size()))continue;auto root=doc.FirstChildElement("root");auto d=root?root->FirstChildElement("device"):nullptr;if(!d)continue;auto title=d->FirstChildElement("friendlyName"),udn=d->FirstChildElement("UDN"),list=d->FirstChildElement("serviceList");if(!list)continue;
        std::string volume_control,volume_service;
        for(auto entry=list->FirstChildElement("service");entry;entry=entry->NextSiblingElement("service")){
            auto type=entry->FirstChildElement("serviceType"),control=entry->FirstChildElement("controlURL");
            if(!type||!control||!type->GetText()||!control->GetText())continue;
            std::string t=type->GetText();if(t.find("urn:schemas-upnp-org:service:RenderingControl:")!=0)continue;
            auto url=resolve_http(location,control->GetText());if(parse_url(url).host!=parse_url(location).host)continue;
            volume_control=url;volume_service=t;
        }
        for(auto s=list->FirstChildElement("service");s;s=s->NextSiblingElement("service")){auto type=s->FirstChildElement("serviceType"),control=s->FirstChildElement("controlURL");if(!type||!control||!type->GetText()||!control->GetText())continue;std::string t=type->GetText();if(t.find("urn:schemas-upnp-org:service:AVTransport:")!=0)continue;auto control_url=resolve_http(location,control->GetText());if(parse_url(control_url).host!=parse_url(location).host)continue;
            out.push_back({{"id","dlna:"+std::string(udn&&udn->GetText()?udn->GetText():location)},{"name",std::string(title&&title->GetText()?title->GetText():"DLNA renderer").substr(0,160)},{"protocol","dlna"},{"host",parse_url(location).host},{"port",parse_url(location).port},{"control",control_url},{"service",t},{"volume_control",volume_control},{"volume_service",volume_service},{"supported",true},{"detail","DLNA 媒体播放"}});break;}
    }catch(...){}}
    return out;
}
}
