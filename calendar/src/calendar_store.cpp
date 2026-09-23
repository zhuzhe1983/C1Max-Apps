#include "calendar_store.hpp"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace calendar {
namespace {
constexpr size_t limit = 1024 * 1024;
std::string trimmed(const std::string &s) {
    auto a=s.find_first_not_of(" \t\r\n"), b=s.find_last_not_of(" \t\r\n");
    return a==std::string::npos ? "" : s.substr(a,b-a+1);
}
bool good_id(const std::string &s) {
    return !s.empty() && s.size()<=64 && s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")==std::string::npos;
}
bool text_ok(const std::string &s, size_t maximum, bool multiline=false) {
    if(s.size()>maximum) return false;
    for(unsigned char c:s) if(c<32 && !(multiline && (c=='\n'||c=='\t'))) return false;
    return true;
}
std::string escaped(const std::string &s) {
    std::string out;
    for(char c:s) switch(c) {
        case '\\': out+="\\\\";break; case '\t':out+="\\t";break;
        case '\n':out+="\\n";break; default:out+=c;
    }
    return out;
}
std::string unescaped(const std::string &s) {
    std::string out;
    for(size_t i=0;i<s.size();++i) {
        if(s[i]!='\\') { out+=s[i];continue; }
        if(++i==s.size())throw std::runtime_error("日程文件包含未完成转义");
        if(s[i]=='n')out+='\n';else if(s[i]=='t')out+='\t';else if(s[i]=='\\')out+='\\';
        else throw std::runtime_error("日程文件包含未知转义");
    }
    return out;
}
std::vector<std::string> fields(const std::string &line) {
    std::vector<std::string> result;size_t a=0;
    while(true) { auto b=line.find('\t',a);result.push_back(unescaped(line.substr(a,b-a)));if(b==std::string::npos)break;a=b+1; }
    return result;
}
void ensure_parent(const std::string &path) {
    auto slash=path.rfind('/'); if(slash==std::string::npos)return;
    std::string parent=path.substr(0,slash);
    for(size_t i=1;i<=parent.size();++i) if(i==parent.size()||parent[i]=='/') {
        auto part=parent.substr(0,i); if(mkdir(part.c_str(),0700)&&errno!=EEXIST)
            throw std::runtime_error("无法创建日历数据目录");
    }
}
void check_data(const Data &data) {
    if(data.events.size()>256||data.sources.size()>max_sources)throw std::runtime_error("最多保存 256 项日程和 16 个订阅");
    std::set<std::string> ids;
    for(const auto &event:data.events) {
        auto error=validate(event);if(!error.empty())throw std::runtime_error(error);
        if(!good_id(event.id)||!ids.insert(event.id).second)throw std::runtime_error("日程标识无效或重复");
    }
    ids.clear();
    for(const auto &source:data.sources) {
        auto error=validate(source);if(!error.empty())throw std::runtime_error(error);
        if(!good_id(source.id)||!ids.insert(source.id).second)throw std::runtime_error("订阅标识无效或重复");
    }
}
std::string ical_text(const std::string &s) {
    std::string out;
    for(char c:s) { if(c=='\n')out+="\\n";else { if(c=='\\'||c==','||c==';')out+='\\';out+=c; } }
    return out;
}
std::string compact_date(Date d) { auto s=date_key(d);s.erase(7,1);s.erase(4,1);return s; }
std::string fold_ics(const std::string &text) {
    std::string out;size_t position=0;
    while(position<text.size()) {
        auto end=text.find("\r\n",position);if(end==std::string::npos)end=text.size();
        size_t capacity=75;
        while(end-position>capacity) {
            size_t count=capacity;
            while(count && (static_cast<unsigned char>(text[position+count])&0xc0)==0x80)--count;
            if(!count)count=capacity;
            out.append(text,position,count);out+="\r\n ";position+=count;capacity=74;
        }
        out.append(text,position,end-position);out+="\r\n";position=end+2;
    }
    return out;
}
}
const std::vector<Source> &builtin_sources() {
    // Exact external ICS endpoints from the user's CardputerZero/Calendar.
    // Lunar is a local algorithm there, not an external subscription.
    static const std::vector<Source> sources={
        {"china-holidays","中国节假日 / 调休","https://rilipro.com/HoliBack/HoliBack.php",false},
        {"japan-holidays","日本节假日","https://calendar.google.com/calendar/ical/ja.japanese%23holiday%40group.v.calendar.google.com/public/basic.ics",false},
        {"us-holidays","美国节假日","https://calendar.google.com/calendar/ical/en.usa%23holiday%40group.v.calendar.google.com/public/basic.ics",false},
        {"uk-holidays","英国节假日","https://calendar.google.com/calendar/ical/en.uk%23holiday%40group.v.calendar.google.com/public/basic.ics",false},
        {"germany-holidays","德国节假日","https://calendar.google.com/calendar/ical/en.german%23holiday%40group.v.calendar.google.com/public/basic.ics",false},
        {"france-holidays","法国节假日","https://calendar.google.com/calendar/ical/en.french%23holiday%40group.v.calendar.google.com/public/basic.ics",false},
        {"almanac","黄历 / 每日宜忌","https://rilipro.com/huangli/yi.ics",false}
    };
    return sources;
}
const Source *find_builtin_source(const Data &data,const Source &preset) {
    for(const auto &source:data.sources)
        if(source.id==preset.id||normalize_url(source.url)==normalize_url(preset.url))return &source;
    return nullptr;
}
Source builtin_source_draft(const Data &data,const std::string &id) {
    for(const auto &preset:builtin_sources())if(preset.id==id) {
        if(auto *existing=find_builtin_source(data,preset))return *existing;
        if(data.sources.size()>=max_sources)throw std::runtime_error("最多添加 16 个 ICS 订阅");
        return preset;
    }
    throw std::runtime_error("未找到内置日历");
}
bool parse_date_text(const std::string &s,Date *date) {
    if(s.size()!=10||s[4]!='-'||s[7]!='-')return false;
    for(size_t i=0;i<s.size();++i)if(i!=4&&i!=7&&(s[i]<'0'||s[i]>'9'))return false;
    Date d{std::stoi(s.substr(0,4)),std::stoi(s.substr(5,2)),std::stoi(s.substr(8,2))};
    if(!valid_date(d)||d.year<1900||d.year>2199)return false;
    *date=d;return true;
}
bool valid_time(const std::string &s) {
    return s.size()==5&&s[2]==':'&&s[0]>='0'&&s[0]<='2'&&s[1]>='0'&&s[1]<='9'&&
        (s[0]!='2'||s[1]<='3')&&s[3]>='0'&&s[3]<='5'&&s[4]>='0'&&s[4]<='9';
}
std::string validate(const LocalEvent &e) {
    if(trimmed(e.title).empty()||!text_ok(e.title,256))return "请填写标题（最多 256 字节）";
    if(!valid_date(e.start)||!valid_date(e.end)||e.start.year<1900||e.end.year>2199||e.end<e.start)
        return "开始/结束日期无效，结束日期不能早于开始日期";
    if(!e.all_day&&(!valid_time(e.start_time)||!valid_time(e.end_time)))return "时间格式为 HH:MM（00:00–23:59）";
    if(!e.all_day&&e.start==e.end&&e.end_time<=e.start_time)return "结束时间必须晚于开始时间";
    if(!text_ok(e.note,4096,true)||!text_ok(e.location,512))return "备注或地点过长，或包含无效字符";
    return "";
}
std::string normalize_url(std::string url) {
    url=trimmed(url);if(url.rfind("webcal://",0)==0)url="https://"+url.substr(9);return url;
}
std::string validate(const Source &source) {
    if(trimmed(source.name).empty()||!text_ok(source.name,128))return "请填写订阅名称";
    auto url=normalize_url(source.url);size_t start=url.rfind("https://",0)==0?8:url.rfind("http://",0)==0?7:0;
    if(!start||url.size()<=start||url.size()>2048||url.find_first_of(" \t\r\n")!=std::string::npos||
        !text_ok(url,2048)||url[start]=='/'||url[start]=='?'||url[start]=='#')return "请填写有效的 http(s) 或 webcal 地址";
    return "";
}
std::string new_id() {
    unsigned char bytes[12];int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(fd<0)throw std::runtime_error("无法生成日程标识");
    size_t n=0;while(n<sizeof bytes) { ssize_t count=read(fd,bytes+n,sizeof bytes-n);if(count<0&&errno==EINTR)continue;if(count<=0){close(fd);throw std::runtime_error("无法读取随机标识");}n+=size_t(count); }close(fd);
    const char *hex="0123456789abcdef";std::string out;for(auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
std::string serialize_data(const Data &data) {
    check_data(data);std::string out="C1MAX_CALENDAR_V1\n";
    for(const auto &e:data.events) out+="E\t"+e.id+"\t"+date_key(e.start)+"\t"+date_key(e.end)+"\t"+
        (e.all_day?"1":"0")+"\t"+escaped(e.start_time)+"\t"+escaped(e.end_time)+"\t"+escaped(e.title)+"\t"+escaped(e.location)+"\t"+escaped(e.note)+"\n";
    for(const auto &s:data.sources)out+="S\t"+s.id+"\t"+(s.enabled?"1":"0")+"\t"+escaped(s.name)+"\t"+escaped(normalize_url(s.url))+"\n";
    if(out.size()>limit)throw std::runtime_error("日历数据超过 1 MiB");return out;
}
Data parse_data(const std::string &text) {
    if(text.size()>limit)throw std::runtime_error("日历数据超过 1 MiB");
    std::stringstream stream(text);std::string line;Data result;
    if(!std::getline(stream,line)||line!="C1MAX_CALENDAR_V1")throw std::runtime_error("无法识别日历数据版本");
    while(std::getline(stream,line)) {
        auto f=fields(line);
        if(f.size()==10&&f[0]=="E") {
            LocalEvent e;e.id=f[1];if(!parse_date_text(f[2],&e.start)||!parse_date_text(f[3],&e.end)||(f[4]!="0"&&f[4]!="1"))throw std::runtime_error("日程日期无效");
            e.all_day=f[4]=="1";e.start_time=f[5];e.end_time=f[6];e.title=f[7];e.location=f[8];e.note=f[9];result.events.push_back(e);
        }else if(f.size()==5&&f[0]=="S"&&(f[2]=="0"||f[2]=="1"))result.sources.push_back({f[1],f[3],f[4],f[2]=="1"});
        else throw std::runtime_error("日历数据格式损坏，未覆盖原文件");
        if(result.events.size()>256||result.sources.size()>max_sources)throw std::runtime_error("日历条目过多");
    }
    check_data(result);return result;
}
std::string read_bounded(const std::string &path,size_t maximum,bool missing_ok) {
    int fd=open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0) { if(missing_ok&&errno==ENOENT)return "";throw std::runtime_error("无法读取日历文件"); }
    struct stat st{};if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||uint64_t(st.st_size)>maximum) { close(fd);throw std::runtime_error("日历文件无效或过大"); }
    std::string out;char buffer[4096];
    while(true) { ssize_t n=read(fd,buffer,sizeof buffer);if(n<0&&errno==EINTR)continue;if(n<0){close(fd);throw std::runtime_error("读取日历失败");}if(!n)break;if(out.size()+size_t(n)>maximum){close(fd);throw std::runtime_error("日历文件超过容量限制");}out.append(buffer,size_t(n)); }
    close(fd);return out;
}
void write_atomic(const std::string &path,const std::string &body) {
    ensure_parent(path);std::string temp=path+".tmp.XXXXXX";std::vector<char> name(temp.begin(),temp.end());name.push_back(0);
    int fd=mkstemp(name.data());if(fd<0)throw std::runtime_error("无法暂存日历数据");
    size_t offset=0;bool ok=true;
    while(offset<body.size()) { ssize_t n=write(fd,body.data()+offset,body.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}offset+=size_t(n); }
    if(ok&&fsync(fd))ok=false;if(close(fd))ok=false;
    if(ok&&rename(name.data(),path.c_str()))ok=false;
    if(!ok){unlink(name.data());throw std::runtime_error("保存失败，原日历文件保留");}
    auto slash=path.rfind('/');auto parent=slash==std::string::npos?".":path.substr(0,slash);
    fd=open(parent.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd>=0){(void)fsync(fd);close(fd);}
}
Data load_data(const std::string &path) {
    auto text=read_bounded(path,limit);
    if(text.empty()) { struct stat st{};if(lstat(path.c_str(),&st)==0)throw std::runtime_error("日历文件为空，已保护原文件");return Data{}; }
    return parse_data(text);
}
void save_data(const std::string &path,const Data &data) { write_atomic(path,serialize_data(data)); }
Event as_event(const LocalEvent &e) {
    Event out;out.id=e.id;out.local=true;out.title=e.title;out.start=e.start;out.end=e.end;out.all_day=e.all_day;
    out.time_text=e.all_day?"":e.start_time+"–"+e.end_time;out.location=e.location;out.description=e.note;return out;
}
std::string export_ics(const std::vector<LocalEvent> &events) {
    std::string out="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//C1Max//Calendar//ZH\r\n";
    for(const auto &e:events) {
        auto error=validate(e);if(!error.empty())throw std::runtime_error(error);
        if(!good_id(e.id))throw std::runtime_error("无法导出没有有效标识的日程");
        out+="BEGIN:VEVENT\r\nUID:"+e.id+"@c1max\r\nSUMMARY:"+ical_text(e.title)+"\r\n";
        if(e.all_day)out+="DTSTART;VALUE=DATE:"+compact_date(e.start)+"\r\nDTEND;VALUE=DATE:"+compact_date(add_days(e.end,1))+"\r\n";
        else { auto a=e.start_time,b=e.end_time;a.erase(2,1);b.erase(2,1);out+="DTSTART:"+compact_date(e.start)+"T"+a+"00\r\nDTEND:"+compact_date(e.end)+"T"+b+"00\r\n"; }
        out+="LOCATION:"+ical_text(e.location)+"\r\nDESCRIPTION:"+ical_text(e.note)+"\r\nEND:VEVENT\r\n";
    }
    return fold_ics(out+"END:VCALENDAR\r\n");
}
} // namespace calendar
