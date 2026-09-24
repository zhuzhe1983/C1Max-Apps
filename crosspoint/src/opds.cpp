#include "opds.hpp"
#include "tinyxml2.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace crosspoint {
namespace {
using tinyxml2::XMLElement;
std::string lower(std::string s) { for (char& c:s) c=char(std::tolower((unsigned char)c)); return s; }
std::string trim(const std::string& s) {
    auto a=s.find_first_not_of(" \t\r\n");
    return a==std::string::npos?"":s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
std::string bounded(std::string s,size_t max) {
    if(s.size()<=max)return s;
    while(max && (static_cast<unsigned char>(s[max])&0xc0)==0x80)--max;
    s.resize(max);return s;
}
const char* local(const char* s) { auto p=std::string(s).find(':'); return p==std::string::npos?s:s+p+1; }
bool named(const XMLElement* e,const char* s) { return std::string(local(e->Name()))==s; }
std::string attribute(const XMLElement* e,const char* s) { auto v=e->Attribute(s);return v?v:""; }
std::string node_text(const tinyxml2::XMLNode* n) {
    std::string s;
    for(auto p=n->FirstChild();p;p=p->NextSibling()) {
        if(p->ToText())s+=p->Value();else if(p->ToElement())s+=node_text(p);
        if(s.size()>2048)break;
    }
    return bounded(trim(s),512);
}
std::string inherited_base(const XMLElement* e,const std::string& base) {
    auto b=attribute(e,"xml:base");return b.empty()?base:resolve_url(base,b);
}
std::string extension(const std::string& mime) {
    auto t=lower(trim(mime.substr(0,mime.find(';'))));
    if(t=="application/epub+zip")return ".epub";
    if(t=="application/vnd.amazon.ebook"||t=="application/x-azw3"||t=="application/x-mobi8-ebook")return ".azw3";
    if(t=="application/x-mobipocket-ebook")return ".mobi";
    if(t=="application/pdf")return ".pdf";
    if(t=="text/plain")return ".txt";
    if(t=="text/markdown"||t=="text/x-markdown")return ".md";
    return {};
}
int rank(const Acquisition& a) {
    const char* order[]={".epub",".azw3",".mobi",".txt",".md",".pdf"};
    for(int i=0;i<6;++i)if(a.extension==order[i])return i;
    return 6;
}
std::string strip_fragment(std::string s) { s.resize(s.find('#')==std::string::npos?s.size():s.find('#'));return s; }
std::string normalize_path(const std::string& path) {
    std::vector<std::string> parts;size_t at=1;
    while(at<=path.size()) {
        auto end=path.find('/',at);if(end==std::string::npos)end=path.size();
        auto part=path.substr(at,end-at);
        if(part=="..") { if(!parts.empty())parts.pop_back(); }
        else if(part!=".")parts.push_back(part);
        if(end==path.size())break;at=end+1;
    }
    std::string out="/";
    for(size_t i=0;i<parts.size();++i){if(i)out+='/';out+=parts[i];}
    if((path.size()>=2 && path.compare(path.size()-2,2,"/.")==0)||
       (path.size()>=3 && path.compare(path.size()-3,3,"/..")==0))
        if(out.back()!='/')out+='/';
    return out;
}
}
std::string url_origin(const std::string& url) {
    if(url.size()>4096)throw std::runtime_error("Catalog URL is too long");
    if(url.rfind("https://",0)!=0&&url.rfind("http://",0)!=0)
        throw std::runtime_error("Use an http:// or https:// catalog URL");
    for(unsigned char c:url)if(c<=32||c==127||c=='\\')throw std::runtime_error("Invalid catalog URL");
    auto begin=url.find("://")+3,end=url.find_first_of("/?#",begin);
    auto authority=url.substr(begin,end-begin);
    if(authority.empty()||authority.find('@')!=std::string::npos)
        throw std::runtime_error("Invalid catalog host (use the username field for login)");
    return lower(url.substr(0,begin)+authority);
}
std::string resolve_url(const std::string& base,const std::string& reference) {
    auto origin=url_origin(base);
    std::string ref=strip_fragment(reference),url;
    if(ref.rfind("http://",0)==0||ref.rfind("https://",0)==0)url=ref;
    else if(ref.rfind("//",0)==0)url=base.substr(0,base.find(':')+1)+ref;
    else {
        auto colon=ref.find(':'),slash=ref.find_first_of("/?");
        if(colon!=std::string::npos&&(slash==std::string::npos||colon<slash))throw std::runtime_error("Unsupported link scheme");
        auto b=strip_fragment(base);auto pos=b.find('/',b.find("://")+3);
        if(pos==std::string::npos||pos>b.find('?'))b=origin+"/"+(b.find('?')==std::string::npos?"":b.substr(b.find('?')));
        if(ref.empty())url=b;
        else if(ref[0]=='?')url=b.substr(0,b.find('?'))+ref;
        else if(ref[0]=='/')url=origin+ref;
        else { b=b.substr(0,b.find('?'));url=b.substr(0,b.find_last_of('/')+1)+ref; }
    }
    origin=url_origin(url);
    auto at=url.find_first_of("/?",url.find("://")+3);
    std::string rest=at==std::string::npos?"/":url.substr(at);
    if(rest[0]=='?')rest='/'+rest;
    auto q=rest.find('?');return origin+normalize_path(rest.substr(0,q))+(q==std::string::npos?"":rest.substr(q));
}
std::string search_url(const std::string& pattern,const std::string& query) {
    std::string encoded;const char* hex="0123456789ABCDEF";
    for(unsigned char c:trim(query)) {
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')encoded+=c;
        else {encoded+='%';encoded+=hex[c>>4];encoded+=hex[c&15];}
    }
    if(encoded.empty())throw std::runtime_error("Enter a title, author or keyword");
    std::string url=pattern;bool replaced=false;
    for(auto token:{std::string("{searchTerms}"),std::string("{searchTerms?}")}) {
        size_t at=0;while((at=url.find(token,at))!=std::string::npos){url.replace(at,token.size(),encoded);at+=encoded.size();replaced=true;}
    }
    if(!replaced||url.find_first_of("{}")!=std::string::npos)throw std::runtime_error("Unsupported catalog search template");
    url_origin(url);return url;
}
std::string book_filename(const OpdsItem& book,const Acquisition& format) {
    auto base=book.author.empty()||book.author=="Unknown"?book.title:book.title+" - "+book.author;
    for(char& c:base)if(static_cast<unsigned char>(c)<32||std::string("/\\:*?\"<>|").find(c)!=std::string::npos)c='_';
    base=bounded(trim(base),150);
    while(!base.empty()&&(base[0]=='.'||base[0]==' '))base.erase(0,1);
    if(base.empty())base="book";
    if(rank(format)==6)throw std::runtime_error("Unsupported book format");
    return base+format.extension;
}
bool parse_feed(const std::string& xml,const std::string& url,Feed& feed,std::string& error) {
    feed={};error.clear();
    try {
        if(xml.size()>1024*1024)throw std::runtime_error("Catalog exceeds the 1 MiB limit");
        if(xml.find("<!DOCTYPE")!=std::string::npos||xml.find("<!ENTITY")!=std::string::npos)
            throw std::runtime_error("Catalog DTD/entities are not supported");
        tinyxml2::XMLDocument doc;
        if(doc.Parse(xml.data(),xml.size())!=tinyxml2::XML_SUCCESS)throw std::runtime_error("Invalid or incomplete OPDS XML");
        auto root=doc.RootElement();if(!root||!named(root,"feed"))throw std::runtime_error("This URL is not an OPDS Atom feed");
        feed.url=resolve_url(url,"");auto base=inherited_base(root,feed.url);
        for(auto e=root->FirstChildElement();e;e=e->NextSiblingElement()) {
            if(named(e,"title"))feed.title=node_text(e);
            else if(named(e,"link")) {
                auto href=attribute(e,"href"),rel=attribute(e,"rel"),type=attribute(e,"type");
                if(href.empty())continue;
                if(rel!="next"&&rel!="previous"&&rel!="prev"&&rel!="start"&&rel!="search")continue;
                if(rel=="search"&&href.find("{searchTerms")==std::string::npos)continue;
                auto full=resolve_url(inherited_base(e,base),href);
                if(rel=="next")feed.next=full;else if(rel=="previous"||rel=="prev")feed.previous=full;
                else if(rel=="start")feed.start=full;else feed.search=full;
            } else if(named(e,"entry")) {
                if(feed.items.size()>=512)throw std::runtime_error("Catalog page has too many entries (maximum 512)");
                OpdsItem item;auto eb=inherited_base(e,base);
                for(auto child=e->FirstChildElement();child;child=child->NextSiblingElement()) {
                    if(named(child,"title"))item.title=node_text(child);
                    else if(named(child,"author")) {
                        for(auto n=child->FirstChildElement();n;n=n->NextSiblingElement())if(named(n,"name")) {
                            if(!item.author.empty())item.author+=", ";item.author=bounded(item.author+node_text(n),512);
                        }
                    } else if(named(child,"link")) {
                        auto href=attribute(child,"href"),mime=attribute(child,"type"),rel=attribute(child,"rel");
                        if(href.empty())continue;
                        auto ext=extension(mime);
                        bool acquire=rel=="http://opds-spec.org/acquisition"||rel=="http://opds-spec.org/acquisition/open-access";
                        bool nav=mime.find("application/atom+xml")!=std::string::npos;
                        if(!nav&&(!acquire||ext.empty()))continue;
                        auto full=resolve_url(inherited_base(child,eb),href);
                        if(acquire&&!ext.empty()) {
                            if(item.formats.size()>=16)continue;
                            uint64_t bytes=0;auto len=attribute(child,"length");
                            if(!len.empty()&&len.size()<20&&len.find_first_not_of("0123456789")==std::string::npos)bytes=strtoull(len.c_str(),nullptr,10);
                            bool duplicate=false;for(auto& f:item.formats)if(f.url==full)duplicate=true;
                            if(!duplicate)item.formats.push_back({full,mime,ext,bytes});
                        } else if(nav&&item.href.empty())item.href=full;
                    }
                }
                if(!item.title.empty()&&(!item.href.empty()||!item.formats.empty())) {
                    std::stable_sort(item.formats.begin(),item.formats.end(),[](const Acquisition&a,const Acquisition&b){return rank(a)<rank(b);});
                    feed.items.push_back(std::move(item));
                }
            }
        }
        return true;
    }catch(const std::exception& e){feed={};error=e.what();return false;}
}
}
