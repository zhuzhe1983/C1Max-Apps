#include "../internal.hpp"
#include <cassert>
#include <iostream>
using namespace casting;
template<typename F>static void fails(F f){bool failed=false;try{f();}catch(...){failed=true;}assert(failed);}
int main(){
    auto u=parse_url("http://192.0.2.1:8080/media?a=1#fragment");assert(u.port==8080&&u.path=="/media?a=1");
    assert(parse_url("https://example.com?x=1").path=="/?x=1");for(auto&s:{"file:///tmp/a","http://evil@host/a","http://host:0/","http://host:65536/","http://a/\r\nInjected: yes"})fails([&]{parse_url(s);});
    Json payload={{"type","LOAD"},{"media",{{"contentId","https://server/video?token=x&v=1"},{"metadata",{{"title","中文 & <title>"}}}}}};
    auto bytes=cast_packet("sender","receiver-0","urn:test",payload);auto decoded=cast_decode(bytes);assert(decoded["payload"]==payload&&decoded["namespace"]=="urn:test");
    bytes.pop_back();fails([&]{cast_decode(bytes);});fails([]{cast_decode({0x32,0xff,0xff,0xff,0xff,0x7f});});fails([]{cast_decode({0});});
    std::string xml="<root><u:Title xmlns:u=\"urn:example\">中文 &amp; &lt;a&gt;</u:Title></root>";assert(xml_text(xml,"Title")=="中文 & <a>");assert(xml_escape("<&\"'>")=="&lt;&amp;&quot;&apos;&gt;");
    auto query=dns_query("_googlecast._tcp.local");assert(dns_decode(query.data(),query.size()).empty());query[4]=255;fails([&]{dns_decode(query.data(),query.size());});
    // Compression-pointer loop and out-of-packet record are untrusted LAN input.
    std::vector<uint8_t> loop(18,0);loop[5]=1;loop[12]=0xc0;loop[13]=12;fails([&]{dns_decode(loop.data(),loop.size());});
    std::vector<uint8_t> record(12,0);record[7]=1;record.insert(record.end(),{0,0,1,0,1,0,0,0,120,0,4,192,0,2,1});auto rows=dns_decode(record.data(),record.size());assert(rows.size()==1&&rows[0].text=="192.0.2.1");record.pop_back();fails([&]{dns_decode(record.data(),record.size());});
    std::cout<<"PASS cast wire: UTF-8, protobuf bounds, compressed DNS cycles/truncation, URLs and XML\n";
}
