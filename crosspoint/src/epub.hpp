#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace crosspoint {
// A single open ZIP index, with only the requested spine item inflated.
class EpubBook {
    struct Member {uint32_t offset,packed,size,crc;uint16_t method,flags;};
    int fd_=-1;uint64_t size_=0;
    std::map<std::string,Member> members_;
    std::vector<std::string> chapters_;
    std::string read_at(uint64_t offset,size_t length) const;
    std::string member(const std::string& name,size_t limit,const std::atomic<bool>& cancel) const;
public:
    EpubBook()=default;
    ~EpubBook();
    EpubBook(const EpubBook&)=delete;
    EpubBook& operator=(const EpubBook&)=delete;
    void open(const std::string& path,const std::atomic<bool>& cancel);
    size_t chapter_count() const{return chapters_.size();}
    std::string chapter(size_t index,const std::atomic<bool>& cancel) const;
};
}
