#include "opds.hpp"
#include "paginate.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace crosspoint;
std::string read(const char* path){std::ifstream f(path);assert(f);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
    std::string text;
    for(int i=0;i<100;++i)text+="中文 mixed words 🚀\n\nparagraph next sentence\n";
    auto fits=[](const std::string& s){unsigned row=1,column=0;for(unsigned char c:s){
        if(c=='\n'){++row;column=0;}else if((c&0xc0)!=0x80){if(++column>16){++row;column=1;}}
    }return row<=5;};
    auto pages=paginate(text,fits);std::string reconstructed;
    assert(pages.size()>10);for(auto& p:pages){assert(!p.empty()&&fits(p));assert((static_cast<unsigned char>(p[0])&0xc0)!=0x80);reconstructed+=p;}
    assert(reconstructed==text);
    reconstructed.clear();for(size_t end=text.size();end;){
        auto previous=previous_page(text,end,fits);assert(!previous.empty()&&fits(previous));
        assert((static_cast<unsigned char>(previous[0])&0xc0)!=0x80);
        end-=previous.size();reconstructed=previous+reconstructed;
    }assert(reconstructed==text);assert(previous_page(text,0,fits).empty());
    const std::string base="https://library.test/opds/page?offset=0";
    assert(resolve_url(base,"?offset=50")=="https://library.test/opds/page?offset=50");
    assert(resolve_url(base,"../book.epub")=="https://library.test/book.epub");
    assert(resolve_url(base,"next.xml")=="https://library.test/opds/next.xml");
    assert(resolve_url("https://library.test/opds/","./next")=="https://library.test/opds/next");
    assert(resolve_url("https://library.test?q=1","?q=2")=="https://library.test/?q=2");
    assert(resolve_url(base,"/get/epub/12/library")=="https://library.test/get/epub/12/library");
    assert(resolve_url(base,"//cdn.test/book.epub")=="https://cdn.test/book.epub");
    assert(resolve_url(base,"#top")==base);
    assert(resolve_url(base,"../../../../x")=="https://library.test/x");
    assert(resolve_url(base,"/a/./b/../c/")=="https://library.test/a/c/");
    for(auto bad:{"file:///etc/passwd","javascript:alert(1)","https://u:p@host/x","https://host/\nfoo"}){
        bool threw=false;try{resolve_url(base,bad);}catch(...){threw=true;}assert(threw);
    }
    assert(search_url("https://library.test/opds/search/{searchTerms}?a=1","中文 & x")=="https://library.test/opds/search/%E4%B8%AD%E6%96%87%20%26%20x?a=1");
    Feed feed;std::string why;
    std::string xml=R"(<a:feed xmlns:a="http://www.w3.org/2005/Atom" xml:base="/catalog/">
      <a:title>书库 &amp; Docs</a:title>
      <a:link rel="next" href="?offset=50&amp;x=1"/>
      <a:link rel="previous" href="previous"/>
      <a:link rel="search" href="search?q={searchTerms}"/>
      <a:entry><a:title>Author directory</a:title><a:link type="application/atom+xml;profile=opds-catalog" href="authors"/></a:entry>
      <a:entry xml:base="books/"><a:title><![CDATA[测试书 <1>]]></a:title><a:author><a:name>A &amp; B</a:name></a:author>
       <a:link rel="http://opds-spec.org/acquisition" type="application/pdf" href="/get/pdf/12/library" length="12345"/>
       <a:link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="file"/>
       <a:link rel="http://opds-spec.org/acquisition" type="application/x-mobi8-ebook" href="kindle"/>
       <a:link type="application/atom+xml" href="detail"/>
       <a:link rel="http://opds-spec.org/image" type="image/jpeg" href="cover.jpg"/>
      </a:entry></a:feed>)";
    assert(parse_feed(xml,base,feed,why));assert(feed.title=="书库 & Docs");assert(feed.items.size()==2);
    assert(feed.items[0].navigation());assert(feed.items[0].href=="https://library.test/catalog/authors");
    auto& book=feed.items[1];assert(!book.navigation()&&book.formats.size()==3);
    assert(book.title=="测试书 <1>"&&book.author=="A & B");
    assert(book.formats[0].extension==".epub"&&book.formats[0].url=="https://library.test/catalog/books/file");
    assert(book.formats[1].extension==".azw3");
    assert(book.formats[2].extension==".pdf"&&book.formats[2].size==12345);
    assert(feed.next=="https://library.test/catalog/?offset=50&x=1");
    assert(feed.previous=="https://library.test/catalog/previous");
    assert(book_filename(book,book.formats[2])=="测试书 _1_ - A & B.pdf");
    OpdsItem path;path.title="../../bad/name";assert(book_filename(path,book.formats[0]).find('/')==std::string::npos);
    path.title=std::string(149,'a')+"中文";auto safe=book_filename(path,book.formats[0]);assert(safe==std::string(149,'a')+".epub");
    assert(parse_feed("<feed xmlns='http://www.w3.org/2005/Atom'><title>Empty</title></feed>",base,feed,why)&&feed.items.empty());
    assert(!parse_feed("<html><body>Login</body></html>",base,feed,why));
    assert(!parse_feed("<!DOCTYPE feed [<!ENTITY x SYSTEM 'file:///etc/passwd'>]><feed/>",base,feed,why));
    for(size_t n=0;n<xml.size();++n)assert(!parse_feed(xml.substr(0,n),base,feed,why));
    assert(!parse_feed(std::string(1024*1024+1,'x'),base,feed,why));
    std::string large="<feed>";for(int n=0;n<513;++n)large+="<entry><title>x</title><link type='application/atom+xml' href='x'/></entry>";large+="</feed>";
    assert(!parse_feed(large,base,feed,why));
    if(argc==3){assert(parse_feed(read(argv[1]),"https://library.test/opds",feed,why));assert(feed.items.size()==9&&!feed.search.empty());
        assert(parse_feed(read(argv[2]),"https://library.test/opds/navcatalog/4f6e6577657374?library_id=library",feed,why));
        assert(feed.items.size()==50&&!feed.next.empty());assert(feed.items[0].formats[0].extension==".pdf");}
    std::cout<<"OPDS XML, namespace/entities, formats, pagination/search, URL resolution, bounded filenames and malformed feeds passed\n";
}
