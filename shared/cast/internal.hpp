#pragma once
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#ifndef MSG_NOSIGNAL
#ifdef __APPLE__
#define MSG_NOSIGNAL 0
#endif
#endif
namespace casting {
using Json=nlohmann::json;
int64_t now_ms();
struct Fd {int n=-1;Fd()=default;explicit Fd(int v):n(v){}~Fd();Fd(const Fd&)=delete;Fd&operator=(const Fd&)=delete;int release(){int v=n;n=-1;return v;}void reset(int v=-1);};
struct Url {std::string scheme,host,path;int port=0;};
Url parse_url(const std::string&);
bool media_url(const std::string&);
int tcp(const std::string&,int,int timeout=2500);
void wait_fd(int fd,short events,int64_t deadline);
void send_all(int fd,const void*,size_t,int64_t deadline);
struct Http {int code=0;std::string body;};
Http http(const std::string &url,const std::string &method="GET",const std::string &body="",const std::vector<std::string>&headers={},int timeout_ms=3500);
std::string xml_escape(const std::string&);
std::string xml_text(const std::string&,const std::string&);
std::vector<uint8_t> cast_packet(const std::string &source,const std::string &dest,const std::string &ns,const Json &payload);
Json cast_decode(const std::vector<uint8_t>&);
struct DnsRecord {std::string name,text;uint16_t type=0,port=0;};
std::vector<DnsRecord> dns_decode(const uint8_t*,size_t);
std::vector<uint8_t> dns_query(const std::string&);
std::vector<Json> discover(int milliseconds=3500);
class Receiver {
public:
    virtual ~Receiver()=default;
    virtual void connect(const Json&device,const std::string&data_dir)=0;
    virtual void load(const Json&media)=0;
    virtual void command(const std::string&action,double value)=0;
    virtual Json poll()=0;
};
std::unique_ptr<Receiver> receiver(const std::string&protocol);
std::unique_ptr<Receiver> cast_receiver();
std::unique_ptr<Receiver> dlna_receiver();
}
