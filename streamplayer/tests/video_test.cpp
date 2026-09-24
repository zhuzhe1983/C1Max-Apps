#include "y4m.hpp"
#include "frame_watchdog.hpp"
#include "../../shared/video_layout.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

static std::string stream(unsigned char luma){
    std::string s="YUV4MPEG2 W4 H2 F20:1 Ip A1:1 C420jpeg\nFRAME\n";
    s.append(8,char(luma));s.append(4,char(128));return s;
}
int main(){
    FrameWatchdog clock;clock.reset(1000);clock.frame(1050);
    assert(!clock.stalled(1049,false)); // poll snapshot taken before frame callback
    assert(!clock.stalled(16050,false)&&clock.stalled(16051,false));
    assert(!clock.stalled(70000,true));clock.resume(70000);
    assert(!clock.stalled(70001,false)&&!clock.stalled(85000,false));
    clock.frame(85001);assert(!clock.stalled(85002,false));
    clock.reset(0xfffffff0);assert(!clock.stalled(20,false));
    assert(clock.stalled(16000,false));
    auto sample=stream(235);unsigned callbacks=0;
    Y4mReader reader;
    auto check=[&](const uint32_t *rgb,int w,int h,int an,int ad){
        assert(w==4&&h==2&&an==1&&ad==1);for(int i=0;i<8;i++)assert(rgb[i]==0xffffffff);++callbacks;
    };
    for(size_t i=0;i<sample.size();i++){
        assert(reader.feed(reinterpret_cast<const uint8_t*>(sample.data()+i),1,check));
        assert(callbacks==(i+1==sample.size()?1u:0u));
    }
    std::string more="FRAME Xtag=value\n";more.append(8,char(16));more.append(4,char(128));
    assert(reader.feed(reinterpret_cast<const uint8_t*>(more.data()),more.size(),[&](const uint32_t *rgb,int,int,int,int){for(int i=0;i<8;i++)assert(rgb[i]==0xff000000);++callbacks;}));
    assert(callbacks==2&&reader.frames()==2);
    // Rendition changes carry a fresh header, even if fragmented one byte at a
    // time. No half-frame or old geometry may reach the compositor.
    std::string switched="YUV4MPEG2 W2 H4 F20:1 Ip A1:1 C420jpeg\nFRAME Xpts=1080000\n";
    switched.append(8,char(235));switched.append(4,char(128));
    for(char byte:switched)assert(reader.feed(reinterpret_cast<const uint8_t*>(&byte),1,[&](const uint32_t*,int w,int h,int,int){assert(w==2&&h==4&&reader.pts()==1080000);++callbacks;}));
    assert(callbacks==3&&reader.frames()==3);
    for(const std::string &bad:std::vector<std::string>{"bad\n","YUV4MPEG2 W4096 H2160\n","YUV4MPEG2 W3 H2\n","YUV4MPEG2 W4 H2 C422\n","YUV4MPEG2 W4 H2 It\n",std::string(1026,'X')}){
        Y4mReader invalid;bool called=false;assert(!invalid.feed(reinterpret_cast<const uint8_t*>(bad.data()),bad.size(),[&](const uint32_t*,int,int,int,int){called=true;}));assert(!called&&!invalid.error().empty());
    }
    // 16:9 source in 800:340: width mode crops top/bottom; fit letterboxes sides.
    VideoLayout layout;layout.configure(320,180,1,1,true);
    assert(layout.xs[0]==0&&layout.xs[799]==319);assert(layout.ys[0]==22&&layout.ys[339]==157);
    layout.configure(320,180,1,1,false);assert(layout.xs[0]==-1&&layout.xs[799]==-1);assert(layout.ys[0]==0&&layout.ys[339]==179);
    // A complete frame, paused or running, must preserve the two control strips
    // and never touch stride padding / memory outside the selected output page.
    layout.configure(4,2,1,1,true);std::vector<uint32_t> rgb(8,0xff11aa44),ui(340*800,0xff3355ee);
    constexpr int stride=1376;std::vector<uint8_t> output(size_t(stride)*800+32,0xa5);
    layout.compose(output.data()+16,stride,rgb.data(),4,ui.data(),true);
    auto pixel=[&](int x,int y){uint32_t c;std::memcpy(&c,output.data()+16+size_t(799-x)*stride+y*4,4);return c;};
    assert(pixel(400,10)==0xff3355ee&&pixel(400,250)==0xff3355ee&&pixel(400,100)==0xff11aa44);
    layout.compose(output.data()+16,stride,rgb.data(),4,ui.data(),false);
    assert(pixel(400,10)==0xff11aa44&&pixel(400,250)==0xff11aa44);
    layout.compose(output.data()+16,stride,rgb.data(),4,ui.data(),true,true);
    assert(pixel(400,100)==0xff3355ee); // Full subtitle selection overlay.
    for(int i=0;i<16;i++)assert(output[i]==0xa5&&output[output.size()-1-i]==0xa5);
    for(int row=0;row<800;row++)for(int pad=1360;pad<stride;pad++)assert(output[16+row*stride+pad]==0xa5);
    std::cout<<"video: fragmented reads, full-frame publication, bounds, crop/fit, controls and stride PASS\n";
}
