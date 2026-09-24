#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace crosspoint {
struct Acquisition {
    std::string url, mime, extension;
    uint64_t size = 0;
};
struct OpdsItem {
    std::string title, author, href;
    std::vector<Acquisition> formats;
    bool navigation() const { return formats.empty(); }
};
struct Feed {
    std::string url, title, search, next, previous, start;
    std::vector<OpdsItem> items;
};
// OPDS links resolve against the current document (including xml:base), not
// the saved server root. These helpers deliberately do not change media URLs.
std::string url_origin(const std::string& url);
std::string resolve_url(const std::string& base, const std::string& reference);
std::string search_url(const std::string& pattern, const std::string& query);
std::string book_filename(const OpdsItem& book, const Acquisition& format);
bool parse_feed(const std::string& xml, const std::string& url, Feed& feed, std::string& error);
}
