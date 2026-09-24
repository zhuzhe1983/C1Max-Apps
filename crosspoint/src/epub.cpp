#include "epub.hpp"
#include "reader.hpp"
#include "tinyxml2.h"
#include "miniz.h"
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace crosspoint {
namespace {
uint16_t u16(const char* p){return uint8_t(p[0])|(uint16_t(uint8_t(p[1]))<<8);}
uint32_t u32(const char* p){return u16(p)|(uint32_t(u16(p+2))<<16);}
void check_cancel(const std::atomic<bool>& c){if(c.load())throw std::runtime_error("Cancelled");}
bool named(const tinyxml2::XMLElement* e,const char* name){auto p=strrchr(e->Name(),':');return strcmp(p?p+1:e->Name(),name)==0;}
const tinyxml2::XMLElement* child(const tinyxml2::XMLElement* e,const char* name){
    for(auto p=e?e->FirstChildElement():nullptr;p;p=p->NextSiblingElement())if(named(p,name))return p;return nullptr;
}
std::string attr(const tinyxml2::XMLElement* e,const char* name){auto s=e?e->Attribute(name):nullptr;return s?s:"";}
std::string package_path(const std::string& base,std::string path){
    path=path.substr(0,path.find_first_of("?#"));
    std::string decoded;for(size_t i=0;i<path.size();++i){
        auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
        if(path[i]=='%'){if(i+2>=path.size()||hex(path[i+1])<0||hex(path[i+2])<0)throw std::runtime_error("Invalid EPUB member URL");decoded+=char(hex(path[i+1])*16+hex(path[i+2]));i+=2;}else decoded+=path[i];
    }
    if(decoded.empty()||decoded[0]=='/'||decoded.find_first_of(":\\")!=std::string::npos||decoded.find('\0')!=std::string::npos)throw std::runtime_error("Unsafe EPUB member path");
    path=base+decoded;std::vector<std::string> parts;size_t at=0;
    while(at<=path.size()){auto end=path.find('/',at);if(end==std::string::npos)end=path.size();auto part=path.substr(at,end-at);
        if(part==".."){if(parts.empty())throw std::runtime_error("EPUB path escapes the archive");parts.pop_back();}
        else if(!part.empty()&&part!=".")parts.push_back(part);
        if(end==path.size())break;at=end+1;
    }
    std::string out;for(auto& p:parts){if(!out.empty())out+='/';out+=p;}return out;
}
void parse_xml(tinyxml2::XMLDocument& doc,const std::string& xml){
    // Normal XHTML may have a public DOCTYPE; container/OPF metadata does not
    // need external entities. TinyXML-2 never resolves external resources.
    if(xml.find("<!ENTITY")!=std::string::npos||doc.Parse(xml.data(),xml.size())!=tinyxml2::XML_SUCCESS)
        throw std::runtime_error("Invalid EPUB metadata XML");
}
}
EpubBook::~EpubBook(){if(fd_>=0)close(fd_);}
std::string EpubBook::read_at(uint64_t offset,size_t length) const{
    if(offset>size_||length>size_-offset)throw std::runtime_error("Truncated EPUB archive");
    std::string out(length,'\0');size_t done=0;
    while(done<length){ssize_t n=pread(fd_,out.data()+done,length-done,off_t(offset+done));if(n<0&&errno==EINTR)continue;if(n<=0)throw std::runtime_error("Cannot read EPUB archive");done+=size_t(n);}return out;
}
std::string EpubBook::member(const std::string& name,size_t limit,const std::atomic<bool>& cancel) const{
    check_cancel(cancel);auto it=members_.find(name);if(it==members_.end())throw std::runtime_error("EPUB is missing a referenced chapter");
    const auto& m=it->second;
    if(m.flags&1)throw std::runtime_error("Encrypted EPUB members are not supported");
    if(m.size>limit||m.packed>8*1024*1024)throw std::runtime_error("EPUB chapter or metadata exceeds the size limit");
    auto h=read_at(m.offset,30);if(u32(h.data())!=0x04034b50||u16(h.data()+8)!=m.method)throw std::runtime_error("Invalid EPUB ZIP member header");
    auto name_size=u16(h.data()+26),extra=u16(h.data()+28);
    if(read_at(uint64_t(m.offset)+30,name_size)!=name)throw std::runtime_error("EPUB ZIP member name mismatch");
    auto packed=read_at(uint64_t(m.offset)+30+name_size+extra,m.packed);check_cancel(cancel);
    std::string out;
    if(m.method==0){if(m.packed!=m.size)throw std::runtime_error("Invalid stored EPUB member");out=std::move(packed);}
    else if(m.method==8){
        out.resize(std::max<uint32_t>(1,m.size));mz_stream stream{};
        if(mz_inflateInit2(&stream,-15)!=MZ_OK)throw std::runtime_error("Cannot allocate EPUB decompressor");
        stream.next_in=reinterpret_cast<const unsigned char*>(packed.data());stream.avail_in=packed.size();
        stream.next_out=reinterpret_cast<unsigned char*>(out.data());stream.avail_out=out.size();
        auto result=mz_inflate(&stream,MZ_FINISH);auto written=stream.total_out,consumed=stream.total_in;mz_inflateEnd(&stream);
        if(result!=MZ_STREAM_END||written!=m.size||consumed!=m.packed)throw std::runtime_error("Corrupt EPUB compressed chapter");out.resize(m.size);
    }else throw std::runtime_error("Unsupported EPUB ZIP compression");
    if(uint32_t(mz_crc32(0,reinterpret_cast<const unsigned char*>(out.data()),out.size()))!=m.crc)throw std::runtime_error("EPUB chapter checksum mismatch");
    check_cancel(cancel);return out;
}
void EpubBook::open(const std::string& path,const std::atomic<bool>& cancel){
    if(fd_>=0){close(fd_);fd_=-1;}members_.clear();chapters_.clear();
    fd_=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW);struct stat st{};
    if(fd_<0||fstat(fd_,&st)||!S_ISREG(st.st_mode)||st.st_size<22)throw std::runtime_error("Cannot open EPUB file");
    size_=st.st_size;auto tail=read_at(size_-std::min<uint64_t>(size_,65557),std::min<uint64_t>(size_,65557));
    size_t end=std::string::npos;
    for(size_t i=tail.size()-22;;--i){if(u32(tail.data()+i)==0x06054b50&&i+22+u16(tail.data()+i+20)==tail.size()){end=i;break;}if(i==0)break;}
    if(end==std::string::npos)throw std::runtime_error("EPUB ZIP directory is missing");
    const char* e=tail.data()+end;auto count=u16(e+10);uint32_t bytes=u32(e+12),offset=u32(e+16);
    if(u16(e+4)||u16(e+6)||u16(e+8)!=count||count==65535||bytes==0xffffffff||offset==0xffffffff)
        throw std::runtime_error("Split/ZIP64 EPUB archives are not supported");
    if(count>12000||bytes>4*1024*1024)throw std::runtime_error("EPUB archive index is too large");
    auto directory=read_at(offset,bytes);size_t at=0;
    for(unsigned i=0;i<count;++i){
        check_cancel(cancel);if(directory.size()-at<46||u32(directory.data()+at)!=0x02014b50)throw std::runtime_error("Invalid EPUB ZIP directory");
        const char* p=directory.data()+at;auto n=u16(p+28),extra=u16(p+30),comment=u16(p+32);
        size_t step=46u+n+extra+comment;if(step>directory.size()-at||n>2048)throw std::runtime_error("Invalid EPUB ZIP directory length");
        Member m{u32(p+42),u32(p+20),u32(p+24),u32(p+16),u16(p+10),u16(p+8)};
        std::string name(p+46,n);if(name.find('\0')!=std::string::npos||!members_.emplace(name,m).second)throw std::runtime_error("Ambiguous EPUB ZIP member");at+=step;
    }
    tinyxml2::XMLDocument container;parse_xml(container,member("META-INF/container.xml",65536,cancel));
    auto files=child(container.RootElement(),"rootfiles");auto rootfile=child(files,"rootfile");
    auto package=package_path("",attr(rootfile,"full-path"));auto slash=package.find_last_of('/');auto base=slash==std::string::npos?"":package.substr(0,slash+1);
    tinyxml2::XMLDocument opf;parse_xml(opf,member(package,2*1024*1024,cancel));
    auto manifest=child(opf.RootElement(),"manifest"),spine=child(opf.RootElement(),"spine");
    if(!manifest||!spine)throw std::runtime_error("EPUB reading order is missing");
    std::map<std::string,std::string> items;
    for(auto item=manifest->FirstChildElement();item;item=item->NextSiblingElement()){
        check_cancel(cancel);if(!named(item,"item"))continue;auto type=attr(item,"media-type");
        if(type=="application/xhtml+xml"||type=="text/html")items[attr(item,"id")]=package_path(base,attr(item,"href"));
    }
    for(auto item=spine->FirstChildElement();item;item=item->NextSiblingElement()){
        if(!named(item,"itemref")||attr(item,"linear")=="no")continue;
        auto it=items.find(attr(item,"idref"));if(it!=items.end())chapters_.push_back(it->second);
        if(chapters_.size()>12000)throw std::runtime_error("Too many EPUB chapters");
    }
    if(chapters_.empty())throw std::runtime_error("EPUB has no readable chapters");
}
std::string EpubBook::chapter(size_t index,const std::atomic<bool>& cancel) const{
    if(index>=chapters_.size())throw std::runtime_error("Invalid EPUB chapter index");
    auto html=member(chapters_[index],2*1024*1024,cancel);
    // Ignore the document head (CSS/title metadata), not the visible body.
    auto lower=html;for(char& c:lower)c=char(std::tolower(static_cast<unsigned char>(c)));
    auto body=lower.find("<body");if(body!=std::string::npos){auto begin=html.find('>',body);if(begin!=std::string::npos)html=html.substr(begin+1);}
    auto text=html_text(html);check_cancel(cancel);return text;
}
}
