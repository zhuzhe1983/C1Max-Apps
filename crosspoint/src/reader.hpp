#pragma once
#include <string>
#include <vector>

namespace crosspoint {
std::string html_text(const std::string &html);
std::string markdown_text(const std::string &markdown);
std::vector<std::string> pages(const std::string &text, size_t page_chars=680);
bool read_book(const std::string &path, std::string &text, std::string &error);
}
