#pragma once
#include "opds.hpp"
#include <atomic>
#include <string>

namespace crosspoint {
struct Server { std::string name,url,user,password; };
// Downloads are capped at 128 MiB; native 32-bit atomics avoid libatomic on MIPS.
struct Progress { std::atomic<uint32_t> bytes{0}, total{0}; std::atomic<bool> saving{false}; };
struct Document { std::string body,url; };
Document fetch_document(const std::string& url,const Server& server,const std::atomic<bool>& cancel,Progress& progress);
std::string download_book(const OpdsItem& book,const Acquisition& format,const Server& server,
                          const std::string& directory,const std::atomic<bool>& cancel,Progress& progress);
}
