#pragma once
#include <atomic>
#include <string>
namespace crosspoint {
// Text extraction is performed only on a cache miss, outside the UI thread.
bool read_pdf(const std::string& path, std::string& text, std::string& error,
              const std::atomic<bool>& cancelled);
}
