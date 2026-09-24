#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <atomic>
using Json = nlohmann::json;
namespace c1 {
std::string root();
std::string data();
std::string read_file(const std::string &path, size_t limit=1048576);
void save_private(const std::string &path, const std::string &body);
std::string encode(const std::string &s);
std::string origin(const std::string &url);
std::string resolve(const std::string &base, const std::string &path);
struct Response { int status=0; std::string body; };
Response http(const std::string &method, const std::string &url,
              const std::vector<std::string> &headers={}, const std::string &body="",
              const std::atomic<bool> *cancel=nullptr);
Json request(const std::string &method, const std::string &url,
             const std::vector<std::string> &headers={}, const Json &body=nullptr);
Json check_updates(const Json &local, const Json &remote);
Json updates();
void cancel_requests();
void reset_requests(int timeout_ms=20000);
}
