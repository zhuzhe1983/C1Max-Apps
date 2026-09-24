#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
#include <limits>

// Bounded streaming decoder for MPlayer's progressive, planar YUV420 output.
// A callback sees only a complete frame, regardless of FIFO read boundaries.
class Y4mReader {
    enum State { Header, Marker, Pixels, Failed } state_=Header;
    std::string line_,error_;
    std::vector<uint8_t> pixels_;
    std::vector<uint32_t> rgb_;
    size_t received_=0;
    int width_=0,height_=0,aspect_n_=1,aspect_d_=1;
    uint64_t count_=0;
    int64_t pts_=std::numeric_limits<int64_t>::min();
    bool fail(const char *why){state_=Failed;error_=why;return false;}
    bool header(){
        width_=height_=0;aspect_n_=aspect_d_=1;
        std::istringstream in(line_);std::string word;in>>word;
        if(word!="YUV4MPEG2")return fail("Missing YUV4MPEG2 header");
        bool progressive=true,color=true;
        while(in>>word){
            try {
                if(word[0]=='W')width_=std::stoi(word.substr(1));
                else if(word[0]=='H')height_=std::stoi(word.substr(1));
                else if(word[0]=='I')progressive=word=="Ip"||word=="I?";
                else if(word[0]=='C')color=word=="C420"||word=="C420jpeg"||word=="C420mpeg2"||word=="C420paldv";
                else if(word[0]=='A'){auto colon=word.find(':');if(colon==std::string::npos)return fail("Invalid pixel aspect ratio");aspect_n_=std::stoi(word.substr(1,colon-1));aspect_d_=std::stoi(word.substr(colon+1));}
            }catch(...){return fail("Invalid YUV frame header");}
        }
        if(width_<2||height_<2||width_>512||height_>288||(width_%2)||(height_%2)||!progressive||!color)
            return fail("Expected progressive YUV420 at up to 512 x 288");
        if(aspect_n_<=0||aspect_d_<=0)aspect_n_=aspect_d_=1;
        if(aspect_n_>1000000||aspect_d_>1000000)return fail("Pixel aspect ratio out of range");
        pixels_.resize(size_t(width_)*height_*3/2);rgb_.resize(size_t(width_)*height_);
        state_=Marker;return true;
    }
    void convert(){
        auto n=size_t(width_)*height_;const auto *u=pixels_.data()+n,*v=u+n/4;
        auto clamp=[](int x){return uint32_t(std::clamp(x,0,255));};
        for(int y=0;y<height_;y++)for(int x=0;x<width_;x++){
            size_t i=size_t(y)*width_+x,c=size_t(y/2)*(width_/2)+x/2;
            int yy=298*(int(pixels_[i])-16),uu=int(u[c])-128,vv=int(v[c])-128;
            rgb_[i]=0xff000000|(clamp((yy+409*vv+128)>>8)<<16)|
                (clamp((yy-100*uu-208*vv+128)>>8)<<8)|clamp((yy+516*uu+128)>>8);
        }
    }
public:
    using Frame=std::function<void(const uint32_t*,int,int,int,int)>;
    bool feed(const uint8_t *data,size_t size,const Frame &frame){
        while(size&&state_!=Failed){
            if(state_==Pixels){
                size_t n=std::min(size,pixels_.size()-received_);
                std::copy_n(data,n,pixels_.data()+received_);data+=n;size-=n;received_+=n;
                if(received_==pixels_.size()){convert();++count_;frame(rgb_.data(),width_,height_,aspect_n_,aspect_d_);state_=Marker;}
            }else{
                char c=char(*data++);--size;
                if(c=='\n'){
                    if(state_==Header||line_.rfind("YUV4MPEG2 ",0)==0){if(!header())return false;}
                    else if(line_=="FRAME"||line_.rfind("FRAME ",0)==0){
                        pts_=std::numeric_limits<int64_t>::min();
                        auto pos=line_.find(" Xpts=");
                        if(pos!=std::string::npos){try{pts_=std::stoll(line_.substr(pos+6));}catch(...){return fail("Invalid frame timestamp");}}
                        received_=0;state_=Pixels;
                    }
                    else return fail("Invalid YUV frame boundary");
                    line_.clear();
                }else{if(line_.size()>=1024)return fail("YUV header too long");line_+=c;}
            }
        }
        return state_!=Failed;
    }
    const std::string& error()const{return error_;}
    uint64_t frames()const{return count_;}
    int64_t pts()const{return pts_;} // MPEG-TS 90 kHz clock, or INT64_MIN.
};
