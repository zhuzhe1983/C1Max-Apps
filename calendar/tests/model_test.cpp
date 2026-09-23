#include "calendar_model.hpp"
#include "calendar_store.hpp"
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <functional>
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace calendar;

int main() {
    assert(days_in_month(2000, 2) == 29);
    assert(days_in_month(2100, 2) == 28);
    assert(days_in_month(2024, 2) == 29);
    assert(!valid_date({2026, 2, 29}));
    assert(!valid_date({2026, 13, 1}));
    assert(valid_date({2024, 2, 29}));
    assert((add_days({2024, 2, 28}, 1) == Date{2024, 2, 29}));
    assert((add_days({2100, 2, 28}, 1) == Date{2100, 3, 1}));
    assert((add_days({2038, 1, 19}, 1) == Date{2038, 1, 20}));
    assert((add_months({2026, 1, 31}, 1) == Date{2026, 2, 28}));
    assert((add_months({2026, 1, 31}, -1) == Date{2025, 12, 31}));
    assert(weekday_monday0({2026, 9, 22}) == 1);
    assert(weekday_monday0({1970, 1, 1}) == 3);
    assert(weekday_monday0({1900, 1, 1}) == 0);
    assert(date_diff_days({2024, 2, 28}, {2024, 3, 1}) == 2);
    // DST must not shift dates or truncate differences to zero days.
    setenv("TZ", "America/New_York", 1);
    tzset();
    assert(date_diff_days({2026, 3, 8}, {2026, 3, 9}) == 1);
    for (int year = 1900; year <= 2199; ++year) {
        for (int month = 1; month <= 12; ++month) {
            auto grid = build_month_grid({year, month, 1}, {year, month, 15}, {});
            assert(grid.size() == 42);
            assert(weekday_monday0(grid.front().date) == 0);
            int selected = 0, in_month = 0;
            for (size_t i = 0; i < grid.size(); ++i) {
                selected += grid[i].selected;
                in_month += grid[i].in_month;
                if (i) assert(date_diff_days(grid[i - 1].date, grid[i].date) == 1);
            }
            assert(selected == 1);
            assert(in_month == days_in_month(year, month));
        }
    }
    const std::string ics =
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nSUMMARY:Meeting\\, Sprint\r\n"
        "DTSTART:20260922T090000Z\r\nDTEND:20260922T100000Z\r\n"
        "DESCRIPTION:Folded\r\n line\\nsecond\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nSUMMARY:Holiday\r\nDTSTART;VALUE=DATE:20260923\r\n"
        "DTEND;VALUE=DATE:20260925\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nSUMMARY:Daily\r\nDTSTART;VALUE=DATE:20260920\r\n"
        "RRULE:FREQ=DAILY;COUNT=3\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    unsigned skipped = 999;
    auto events = parse_ics_events(ics, {2026, 9, 1}, {2026, 9, 30}, &skipped);
    assert(skipped == 0);
    assert(events.size() == 5);
    assert(events_for_date(events, {2026, 9, 22}).size() == 2);
    assert(events_for_date(events, {2026, 9, 24}).size() == 1);
    assert(events_for_date(events, {2026, 9, 25}).empty());
    assert(events[3].title == "Meeting, Sprint");
    assert(events[3].time_text == "09:00 UTC");
    assert(events[3].description == "Foldedline\nsecond");
    auto grid = build_month_grid({2026, 9, 1}, {2026, 9, 22}, events);
    for (auto &day : grid) if (day.selected) assert(day.event_count == 2);
    auto old_daily = parse_ics_events("BEGIN:VEVENT\nDTSTART:19000101\nRRULE:FREQ=DAILY\nEND:VEVENT", {2199, 9, 1}, {2199, 9, 30});
    assert(old_daily.size() == 30);
    auto malformed = parse_ics_events("END:VEVENT\nBEGIN:VEVENT\nDTSTART:20260230\nEND:VEVENT\nBEGIN:VEVENT\nDTSTART:20260922\nRRULE:FREQ=WEEKLY;BYDAY=MO\nEND:VEVENT", {2026, 9, 1}, {2026, 9, 30}, &skipped);
    assert(malformed.empty());
    assert(skipped == 2);
    auto monthly = parse_ics_events("BEGIN:VEVENT\nDTSTART:20260131\nRRULE:FREQ=MONTHLY;COUNT=3\nEND:VEVENT", {2026, 1, 1}, {2026, 3, 31});
    assert(monthly.size() == 3);
    assert((monthly[1].start == Date{2026, 2, 28}));
    assert((monthly[2].start == Date{2026, 3, 31}));
    auto until = parse_ics_events("BEGIN:VEVENT\nDTSTART:20260920\nRRULE:FREQ=DAILY;UNTIL=20260922\nEND:VEVENT", {2026, 9, 1}, {2026, 9, 30});
    assert(until.size() == 3);
    auto rejected = parse_ics_events(std::string(256 * 1024 + 1, 'x'), {2026, 9, 1}, {2026, 9, 30}, &skipped);
    assert(rejected.empty() && skipped == 1);

    // Local CRUD model: dates, times, UTF-8 notes, and stable IDs round-trip.
    Date parsed{};
    assert(parse_date_text("2026-09-22", &parsed));
    assert(!parse_date_text("2026-02-29", &parsed));
    assert(!parse_date_text("2026-9-22", &parsed));
    assert(!parse_date_text("2200-01-01", &parsed));
    assert(valid_time("00:00") && valid_time("23:59"));
    assert(!valid_time("24:00") && !valid_time("12:60") && !valid_time("9:00"));
    LocalEvent personal;
    personal.id = new_id(); personal.title = "团队会议";
    personal.start = {2026,9,22}; personal.end = {2026,9,22};
    personal.start_time = "09:00"; personal.end_time = "10:30";
    personal.location = "Room A"; personal.note = "备注\\路径\n第二行\t中文";
    assert(validate(personal).empty());
    Data data; data.events.push_back(personal);
    data.sources.push_back({new_id(), "工作日历", "webcal://example.com/calendar.ics", false});
    auto saved = serialize_data(data);
    auto loaded = parse_data(saved);
    assert(loaded.events.size() == 1 && loaded.sources.size() == 1);
    assert(loaded.events[0].note == personal.note);
    assert(loaded.events[0].title == personal.title && loaded.events[0].id == personal.id);
    assert(loaded.sources[0].url == "https://example.com/calendar.ics");
    assert(!loaded.sources[0].enabled);
    auto local_display = as_event(personal);
    assert(local_display.local && local_display.id == personal.id);
    assert(local_display.description == personal.note);
    auto invalid = personal;
    invalid.title = "   "; assert(!validate(invalid).empty());
    invalid = personal; invalid.end_time = "08:00"; assert(!validate(invalid).empty());
    invalid = personal; invalid.end = {2026,9,21}; assert(!validate(invalid).empty());
    invalid = personal; invalid.all_day = true; invalid.start_time = invalid.end_time = "";
    assert(validate(invalid).empty());
    Source unsafe{new_id(), "Bad source", "file:///etc/passwd", true};
    assert(!validate(unsafe).empty());
    unsafe.url = "https://example.com/\r\nInjected"; assert(!validate(unsafe).empty());
    const auto throws = [](const std::function<void()> &call) {
        try { call(); } catch (const std::exception &) { return true; } return false;
    };
    auto duplicate = data; duplicate.events.push_back(personal);
    assert(throws([&]{ serialize_data(duplicate); }));
    assert(throws([&]{ parse_data("C1MAX_CALENDAR_V1\nE\tbroken\n"); }));
    assert(throws([&]{ parse_data("C1MAX_CALENDAR_V2\n"); }));
    auto too_many = data;
    for(size_t i=0;i<max_sources;i++)too_many.sources.push_back({"source"+std::to_string(i),"Feed","https://example.com/feed.ics",false});
    assert(throws([&]{ serialize_data(too_many); }));
    too_many.sources.pop_back();assert(parse_data(serialize_data(too_many)).sources.size()==max_sources);
    // Existing V1 users retain all eight custom subscriptions and local events;
    // the seven opt-in presets fit alongside them without seeding the database.
    Data migrated=data;
    for(int i=0;i<7;i++)migrated.sources.push_back({"old"+std::to_string(i),"Custom","https://example.com/"+std::to_string(i)+".ics",true});
    const auto before_presets=serialize_data(migrated);
    assert(builtin_sources().size()==7);
    for(const auto &preset:builtin_sources()) {
        assert(!preset.enabled && validate(preset).empty());
        auto draft=builtin_source_draft(migrated,preset.id);
        assert(!draft.enabled && draft.id==preset.id && draft.url==preset.url);
        assert(serialize_data(migrated)==before_presets); // Viewing never adds/enables/saves.
    }
    for(const auto &preset:builtin_sources())migrated.sources.push_back(builtin_source_draft(migrated,preset.id));
    auto migrated_read=parse_data(serialize_data(migrated));
    assert(migrated_read.sources.size()==15 && migrated_read.events[0].note==personal.note);
    auto customized=builtin_sources()[0];customized.name="My holidays";customized.enabled=true;
    customized.id="existing-custom-id";customized.url="webcal://"+customized.url.substr(8);
    Data existing;existing.sources.push_back(customized);
    auto selected_preset=builtin_source_draft(existing,builtin_sources()[0].id);
    assert(selected_preset.id==customized.id && selected_preset.name==customized.name && selected_preset.enabled);
    assert(find_builtin_source(existing,builtin_sources()[0])==&existing.sources[0]);
    existing.sources.clear();existing=parse_data(serialize_data(existing));
    assert(existing.sources.empty() && !builtin_source_draft(existing,builtin_sources()[0].id).enabled);
    assert(throws([&]{builtin_source_draft(existing,"unknown");}));
    assert(throws([&]{builtin_source_draft(too_many,builtin_sources()[0].id);}));
    too_many.sources[0]=customized;
    assert(builtin_source_draft(too_many,builtin_sources()[0].id).id==customized.id); // Edits still allowed when full.
    Data event_limit;
    for(int i=0;i<256;i++){auto entry=personal;entry.id="event"+std::to_string(i);event_limit.events.push_back(entry);}
    assert(parse_data(serialize_data(event_limit)).events.size()==256);
    auto extra=personal;extra.id="event256";event_limit.events.push_back(extra);
    assert(throws([&]{serialize_data(event_limit);}));
    auto exported = export_ics(data.events);
    auto exported_events = parse_ics_events(exported,{2026,9,1},{2026,9,30});
    assert(exported_events.size()==1 && exported_events[0].title==personal.title);
    assert(exported_events[0].description==personal.note);
    auto long_event=personal;long_event.note=std::string(300,'x')+"中文中文中文中文中文中文中文中文中文中文";
    auto folded_export=export_ics({long_event});
    size_t line_start=0;
    while(line_start<folded_export.size()){auto line_end=folded_export.find("\r\n",line_start);assert(line_end!=std::string::npos&&line_end-line_start<=75);line_start=line_end+2;}
    assert(parse_ics_events(folded_export,{2026,9,1},{2026,9,30})[0].description==long_event.note);
    auto all_day=personal;all_day.all_day=true;all_day.end={2026,9,24};
    auto imported_all_day=parse_ics_events(export_ics({all_day}),{2026,9,1},{2026,9,30});
    assert(imported_all_day.size()==1 && (imported_all_day[0].end==Date{2026,9,24}));

    char directory[]="/tmp/c1max-calendar-store.XXXXXX";
    assert(mkdtemp(directory));
    const std::string path=std::string(directory)+"/calendar.db";
    assert(load_data(path).events.empty());
    save_data(path,data);
    struct stat info{};assert(stat(path.c_str(),&info)==0 && (info.st_mode&0777)==0600);
    assert(load_data(path).events[0].note==personal.note);
    auto old_bytes=read_bounded(path,1024*1024);
    assert(throws([&]{save_data(path,duplicate);}));
    assert(read_bounded(path,1024*1024)==old_bytes);
    // Simulate a real partial-write failure; the old file must remain intact.
    struct rlimit original_limit{}, short_limit{};
    assert(getrlimit(RLIMIT_FSIZE,&original_limit)==0);
    short_limit=original_limit;short_limit.rlim_cur=128;
    auto old_signal=std::signal(SIGXFSZ,SIG_IGN);
    assert(setrlimit(RLIMIT_FSIZE,&short_limit)==0);
    bool partial_failed=throws([&]{write_atomic(path,std::string(4096,'x'));});
    assert(setrlimit(RLIMIT_FSIZE,&original_limit)==0);
    std::signal(SIGXFSZ,old_signal);
    assert(partial_failed && read_bounded(path,1024*1024)==old_bytes);
    auto edited=data;edited.events[0].title="Updated";save_data(path,edited);
    assert(load_data(path).events[0].title=="Updated");
    edited.events.clear();save_data(path,edited);
    assert(load_data(path).events.empty() && load_data(path).sources.size()==1);
    assert(throws([&]{read_bounded(path,4);}));
    write_atomic(path,"corrupt");assert(throws([&]{load_data(path);}));
    assert(read_bounded(path,100)=="corrupt");
    write_atomic(path,"");assert(throws([&]{load_data(path);}));
    unlink(path.c_str());rmdir(directory);
    std::cout << "Calendar: Gregorian 1900–2199, DST/2038, 42-day grids and ICS tests passed\n";
    std::cout << "Calendar: local CRUD, validation, export, private atomic persistence, corruption and source tests passed\n";
    std::cout << "Calendar: opt-in builtins, old data preservation, URL deduplication, deletion and capacity tests passed\n";
}
