#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
namespace bili {
// One current MP4, one explicitly selected receiver. Never an open URL proxy.
class CastRelay {
public:
    CastRelay()=default;
    ~CastRelay();
    CastRelay(const CastRelay&)=delete;
    CastRelay&operator=(const CastRelay&)=delete;
    std::string start(const std::string &upstream,const std::string &receiver_ipv4);
    void stop();
private:
    int listener_=-1;
    std::atomic<bool> stopping_{false};
    std::atomic<uint64_t> size_{0};
    std::thread thread_;
    std::string upstream_,request_path_,peer_;
    void serve();
    void transfer(int fd);
};
}
