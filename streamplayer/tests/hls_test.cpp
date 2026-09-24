#include "hls.hpp"
#include "stream_options.hpp"
#include <cassert>
#include <iostream>
int main(){
    auto p=hls::parse("#EXTM3U\r\n#EXT-X-MEDIA-SEQUENCE:0\r\n#EXTINF:3.000,\r\na.ts?x=1\r\n#EXTINF:2.5,\r\nb.ts\r\n#EXT-X-ENDLIST\r\n");
    assert(p.ended&&p.segments.size()==2&&p.segments[1].start==30000000);
    assert(hls::containing(p,29999999)==0&&hls::containing(p,30000000)==1);
    auto m=hls::parse("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=464000\nmain.m3u8\n");assert(m.variant=="main.m3u8");
    assert(hls::resolve("https://host/emby/Videos/42/main.m3u8?token=secret","hls1/main/7.ts?PlaySessionId=x")=="https://host/emby/Videos/42/hls1/main/7.ts?PlaySessionId=x");
    assert(hls::resolve("http://host/a/b/main.m3u8","../seg.ts")=="http://host/a/seg.ts");
    assert(hls::resolve("http://host/a/main.m3u8","/emby/seg.ts")=="http://host/emby/seg.ts");
    for(auto bad:{"", "<html>error</html>","#EXTM3U\n#EXTINF:nan,\nx.ts\n","#EXTM3U\n#EXTINF:16,\nx.ts\n","#EXTM3U\n#EXTINF:-1,\nx.ts\n","#EXTM3U\n#EXTINF:3,\n","#EXTM3U\n#EXT-X-KEY:METHOD=AES-128\n","#EXTM3U\nx.ts\n"}){
        bool rejected=false;try{hls::parse(bad);}catch(...){rejected=true;}assert(rejected);
    }
    std::string ts(188*3,'x');for(size_t i=0;i<ts.size();i+=188)ts[i]=0x47;
    assert(hls::transport_stream(ts));ts[188]=0;assert(!hls::transport_stream(ts));assert(!hls::transport_stream("error page"));
    StreamOptions width,fit;fit.width_fill=false;assert(width.max_width()==400&&width.max_height()==288&&fit.max_height()==170&&width!=fit);
    std::cout<<"HLS: playlist bounds, segment clock, nested URLs, invalid streams and rendition sizes PASS\n";
}
