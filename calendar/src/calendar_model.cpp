#include "calendar_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <map>
#include <sstream>

namespace calendar {
namespace {
// Gregorian civil arithmetic avoids DST and 32-bit time_t/2038 limitations.
int day_number(Date date) {
    int y = date.year - (date.month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yy = unsigned(y - era * 400);
    unsigned m = unsigned(date.month + (date.month > 2 ? -3 : 9));
    unsigned doy = (153 * m + 2) / 5 + unsigned(date.day) - 1;
    return era * 146097 + int(yy * 365 + yy / 4 - yy / 100 + doy) - 719468;
}
Date from_day_number(int days) {
    days += 719468;
    int era = (days >= 0 ? days : days - 146096) / 146097;
    unsigned doe = unsigned(days - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year = int(yoe) + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    int day = int(doy - (153 * mp + 2) / 5 + 1);
    int month = int(mp) + (mp < 10 ? 3 : -9);
    return {year + (month <= 2), month, day};
}
std::string trim_copy(const std::string &value) {
    size_t a = 0, b = value.size();
    while (a < b && std::isspace(static_cast<unsigned char>(value[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(value[b - 1]))) --b;
    return value.substr(a, b - a);
}
std::string upper_copy(std::string value) {
    for (char &ch : value) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return value;
}
bool parse_int(const std::string &value, int *out) {
    if (value.empty() || value.size() > 9) return false;
    for (char c : value) if (c < '0' || c > '9') return false;
    char *end = nullptr;
    long n = std::strtol(value.c_str(), &end, 10);
    if (*end || n > std::numeric_limits<int>::max()) return false;
    *out = int(n);
    return true;
}
bool parse_ical_date(const std::string &value, Date *date,
                     std::string *time_text, bool *all_day) {
    if (value.size() != 8 && value.size() != 15 && value.size() != 16) return false;
    int y, m, d;
    if (!parse_int(value.substr(0, 4), &y) || !parse_int(value.substr(4, 2), &m) ||
        !parse_int(value.substr(6, 2), &d) || !valid_date({y, m, d})) return false;
    bool timed = value.size() > 8;
    if (timed) {
        int hour, minute, second;
        if (value[8] != 'T' || (value.size() == 16 && value[15] != 'Z') ||
            !parse_int(value.substr(9, 2), &hour) || hour > 23 ||
            !parse_int(value.substr(11, 2), &minute) || minute > 59 ||
            !parse_int(value.substr(13, 2), &second) || second > 60) return false;
    }
    *date = {y, m, d};
    if (all_day) *all_day = !timed;
    if (time_text) *time_text = timed ? value.substr(9, 2) + ":" + value.substr(11, 2) +
                                      (value.size() == 16 ? " UTC" : "") : "";
    return true;
}
std::string ics_unescape(const std::string &value) {
    std::string out;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size()) {
            char next = value[++i];
            out.push_back(next == 'n' || next == 'N' ? '\n' : next);
        } else if (static_cast<unsigned char>(value[i]) >= 32 || value[i] == '\n' || value[i] == '\t') {
            out.push_back(value[i]);
        }
    }
    return trim_copy(out);
}
std::vector<std::string> unfold_ics_lines(const std::string &ics) {
    std::vector<std::string> lines;
    std::stringstream ss(ics);
    std::string raw;
    while (std::getline(ss, raw)) {
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        if (!raw.empty() && (raw[0] == ' ' || raw[0] == '\t') && !lines.empty())
            lines.back() += raw.substr(1);
        else lines.push_back(raw);
    }
    return lines;
}
struct RawEvent {
    Event event;
    bool has_start = false, has_end = false, unsupported = false;
    std::string rrule;
};
bool intersects(Date a, Date b, Date c, Date d) { return a <= d && c <= b; }
bool append_expanded_event(std::vector<Event> &events, const RawEvent &raw,
                           Date window_start, Date window_end) {
    if (!raw.has_start || raw.unsupported) return false;
    Event event = raw.event;
    if (!raw.has_end) event.end = event.start;
    if (event.end < event.start) return false;
    if (event.all_day && raw.has_end && event.end != event.start)
        event.end = add_days(event.end, -1);  // ICS all-day DTEND is exclusive.
    int duration = date_diff_days(event.start, event.end);
    if (duration > 3660) return false;
    if (event.title.empty()) event.title = "未命名日程";
    if (raw.rrule.empty()) {
        if (intersects(event.start, event.end, window_start, window_end)) events.push_back(event);
        return true;
    }
    std::map<std::string, std::string> rule;
    std::stringstream ss(raw.rrule);
    std::string item;
    while (std::getline(ss, item, ';')) {
        auto eq = item.find('=');
        if (eq == std::string::npos) return false;
        auto key = upper_copy(item.substr(0, eq));
        if (key != "FREQ" && key != "INTERVAL" && key != "COUNT" && key != "UNTIL") return false;
        if (rule.count(key)) return false;
        rule[key] = upper_copy(item.substr(eq + 1));
    }
    const std::string &freq = rule["FREQ"];
    if (freq != "DAILY" && freq != "WEEKLY" && freq != "MONTHLY" && freq != "YEARLY") return false;
    int interval = 1, count = 1000000;
    if (rule.count("INTERVAL") && (!parse_int(rule["INTERVAL"], &interval) || interval < 1 || interval > 366)) return false;
    if (rule.count("COUNT") && (!parse_int(rule["COUNT"], &count) || count < 1)) return false;
    Date until = window_end;
    if (rule.count("UNTIL") && !parse_ical_date(rule["UNTIL"], &until, nullptr, nullptr)) return false;
    int step_days = freq == "DAILY" ? interval : freq == "WEEKLY" ? interval * 7 : 0;
    int step_months = freq == "MONTHLY" ? interval : interval * 12;
    int skip = std::max(0, date_diff_days(event.start, window_start) - duration);
    int index = step_days ? skip / step_days :
        std::max(0, ((window_start.year - event.start.year) * 12 + window_start.month - event.start.month) / step_months - duration / 28 - 1);
    for (; index < count && events.size() < 512; ++index) {
        Date start = step_days ? add_days(event.start, index * step_days) : add_months(event.start, index * step_months);
        if (window_end < start || until < start) break;
        Date end = add_days(start, duration);
        if (intersects(start, end, window_start, window_end)) {
            Event occurrence = event;
            occurrence.start = start;
            occurrence.end = end;
            events.push_back(std::move(occurrence));
        }
    }
    return true;
}
}  // namespace

bool operator==(const Date &a, const Date &b) { return a.year == b.year && a.month == b.month && a.day == b.day; }
bool operator!=(const Date &a, const Date &b) { return !(a == b); }
bool operator<(const Date &a, const Date &b) {
    if (a.year != b.year) return a.year < b.year;
    if (a.month != b.month) return a.month < b.month;
    return a.day < b.day;
}
bool operator<=(const Date &a, const Date &b) { return a < b || a == b; }
int days_in_month(int year, int month) {
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1 || month > 12) return 0;
    if (month == 2) return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0) ? 29 : 28;
    return days[month - 1];
}
bool valid_date(Date date) {
    return date.year >= 1 && date.year <= 9999 && date.day >= 1 && date.day <= days_in_month(date.year, date.month);
}
Date today_local() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
    if (!localtime_r(&now, &local)) return {1970, 1, 1};
    return {local.tm_year + 1900, local.tm_mon + 1, local.tm_mday};
}
Date add_days(Date date, int days) { return from_day_number(day_number(date) + days); }
Date add_months(Date date, int months) {
    int index = date.year * 12 + date.month - 1 + months;
    int year = index >= 0 ? index / 12 : (index - 11) / 12;
    Date out = {year, index - year * 12 + 1, date.day};
    out.day = std::min(out.day, days_in_month(out.year, out.month));
    return out;
}
int weekday_monday0(Date date) { int n = (day_number(date) + 3) % 7; return n < 0 ? n + 7 : n; }
int date_diff_days(Date a, Date b) { return day_number(b) - day_number(a); }
std::string date_key(Date date) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", date.year, date.month, date.day);
    return buffer;
}
std::vector<Event> events_for_date(const std::vector<Event> &events, Date date) {
    std::vector<Event> out;
    for (const auto &event : events) if (event.start <= date && date <= event.end) out.push_back(event);
    return out;
}
std::vector<DayInfo> build_month_grid(Date month, Date selected, const std::vector<Event> &events) {
    Date first{month.year, month.month, 1};
    Date start = add_days(first, -weekday_monday0(first));
    Date today = today_local();
    std::vector<DayInfo> out;
    out.reserve(42);
    for (int i = 0; i < 42; ++i) {
        Date date = add_days(start, i);
        int count = 0;
        for (const auto &event : events) if (event.start <= date && date <= event.end) ++count;
        out.push_back({date, date.month == month.month, date == today, date == selected, count});
    }
    return out;
}
std::vector<Event> parse_ics_events(const std::string &ics, Date window_start,
                                   Date window_end, unsigned *skipped) {
    if (skipped) *skipped = 0;
    std::vector<Event> events;
    if (ics.size() > 256 * 1024) { if (skipped) ++*skipped; return events; }
    RawEvent raw;
    bool in_event = false;
    size_t components = 0;
    for (const auto &line : unfold_ics_lines(ics)) {
        auto upper = upper_copy(line);
        if (upper == "BEGIN:VEVENT") {
            if (++components > 2048) { if (skipped) ++*skipped; break; }
            if (in_event && skipped) ++*skipped;
            raw = RawEvent{};
            in_event = true;
            continue;
        }
        if (upper == "END:VEVENT") {
            if (in_event && !append_expanded_event(events, raw, window_start, window_end) && skipped) ++*skipped;
            in_event = false;
            if (events.size() >= 512) { if (skipped) ++*skipped; break; }
            continue;
        }
        if (!in_event) continue;
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto property = upper_copy(line.substr(0, colon));
        auto value = line.substr(colon + 1);
        auto name = property.substr(0, property.find(';'));
        if (name == "SUMMARY") raw.event.title = ics_unescape(value.substr(0, 1024));
        else if (name == "LOCATION") raw.event.location = ics_unescape(value.substr(0, 1024));
        else if (name == "DESCRIPTION") raw.event.description = ics_unescape(value.substr(0, 4096));
        else if (name == "DTSTART") raw.has_start = parse_ical_date(value, &raw.event.start, &raw.event.time_text, &raw.event.all_day);
        else if (name == "DTEND") {
            raw.has_end = parse_ical_date(value, &raw.event.end, nullptr, nullptr);
            if (!raw.has_end) raw.unsupported = true;
        } else if (name == "RRULE") raw.rrule = value;
        else if (name == "EXDATE" || name == "RDATE" || name == "RECURRENCE-ID") raw.unsupported = true;
        if (name == "DTSTART" && property.find("TZID=") != std::string::npos)
            raw.event.time_text += " [" + property.substr(property.find("TZID=") + 5) + "]";
    }
    if (in_event && skipped) ++*skipped;
    std::sort(events.begin(), events.end(), [](const Event &a, const Event &b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.time_text != b.time_text) return a.time_text < b.time_text;
        return a.title < b.title;
    });
    return events;
}
}  // namespace calendar
