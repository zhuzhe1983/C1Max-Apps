#pragma once

#include <string>
#include <vector>

// Adapted from CardputerZero/Calendar/main/src/calendar_model.cpp.
// The C1Max port keeps the calendar/ICS model independent of LVGL and the network.
namespace calendar {
struct Date { int year, month, day; };
struct Event {
    std::string title, location, description, time_text;
    Date start{}, end{};
    bool all_day = true;
    std::string id, source_id;
    bool local = false;
};
struct DayInfo {
    Date date;
    bool in_month, today, selected;
    int event_count;
};
bool operator==(const Date &, const Date &);
bool operator!=(const Date &, const Date &);
bool operator<(const Date &, const Date &);
bool operator<=(const Date &, const Date &);
bool valid_date(Date);
Date today_local();
Date add_days(Date, int);
Date add_months(Date, int);
int days_in_month(int year, int month);
int weekday_monday0(Date);
int date_diff_days(Date start, Date end);
std::string date_key(Date);
std::vector<DayInfo> build_month_grid(Date month, Date selected,
                                    const std::vector<Event> &events);
std::vector<Event> events_for_date(const std::vector<Event> &, Date);
std::vector<Event> parse_ics_events(const std::string &ics, Date window_start,
                                   Date window_end, unsigned *skipped = nullptr);
}  // namespace calendar
