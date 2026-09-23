#pragma once
#include "calendar_model.hpp"
#include <string>
#include <vector>
namespace calendar {
struct LocalEvent {
    std::string id, title, start_time = "09:00", end_time = "10:00", location, note;
    Date start{}, end{};
    bool all_day = false;
};
struct Source { std::string id, name, url; bool enabled = true; };
struct Data { std::vector<LocalEvent> events; std::vector<Source> sources; };
inline constexpr size_t max_sources = 16;
// Optional online presets ported from CardputerZero Calendar. No startup fetch.
const std::vector<Source> &builtin_sources();
const Source *find_builtin_source(const Data &, const Source &preset);
Source builtin_source_draft(const Data &, const std::string &preset_id);
bool parse_date_text(const std::string &, Date *);
bool valid_time(const std::string &);
std::string validate(const LocalEvent &);
std::string validate(const Source &);
std::string normalize_url(std::string);
std::string new_id();
std::string serialize_data(const Data &);
Data parse_data(const std::string &);
Data load_data(const std::string &path);
void save_data(const std::string &path, const Data &);
std::string read_bounded(const std::string &, size_t limit, bool missing_ok = true);
void write_atomic(const std::string &, const std::string &);
Event as_event(const LocalEvent &);
std::string export_ics(const std::vector<LocalEvent> &);
} // namespace calendar
