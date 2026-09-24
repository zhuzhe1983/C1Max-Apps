#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace crosspoint {
// The UI supplies the same font/width/height measurement used by its label.
// Bound work per page and preserve every byte, including paragraph breaks.
template<class Fits>
std::string next_page(const std::string& text,size_t start,Fits fits) {
    if(start>=text.size())return {};
        std::vector<size_t> boundaries{0};size_t pos=start;
        while(pos<text.size()&&boundaries.size()<=2048) {
            unsigned char c=text[pos];size_t n=c<0x80?1:c<0xe0?2:c<0xf0?3:4;
            n=std::min(n,text.size()-pos);
            for(size_t i=1;i<n;++i)if((static_cast<unsigned char>(text[pos+i])&0xc0)!=0x80){n=1;break;}
            pos+=n;boundaries.push_back(pos-start);
        }
        size_t low=1,high=boundaries.size()-1,best=0;
        while(low<=high){size_t mid=(low+high)/2;
            if(fits(text.substr(start,boundaries[mid]))){best=mid;low=mid+1;}else high=mid-1;
        }
        size_t bytes=boundaries[std::max<size_t>(1,best)];
        // Avoid chopping an English word if a nearby whitespace boundary fits.
        if(start+bytes<text.size()&&text[start+bytes]!=' '&&text[start+bytes]!='\n') {
            auto end=text.find_last_of(" \t\n",start+bytes-1);
            if(end!=std::string::npos&&end>=start+bytes*4/5)bytes=end+1-start;
        }
    return text.substr(start,bytes);
}
template<class Fits>
std::string previous_page(const std::string& text,size_t end,Fits fits) {
    end=std::min(end,text.size());if(!end)return {};
    std::vector<size_t> starts{end};size_t pos=end;
    while(pos&&starts.size()<=2048){
        --pos;while(pos&&(static_cast<unsigned char>(text[pos])&0xc0)==0x80)--pos;
        starts.push_back(pos);
    }
    size_t low=1,high=starts.size()-1,best=1;
    while(low<=high){auto mid=(low+high)/2;
        if(fits(text.substr(starts[mid],end-starts[mid]))){best=mid;low=mid+1;}else high=mid-1;
    }
    return text.substr(starts[best],end-starts[best]);
}
template<class Fits>
std::vector<std::string> paginate(const std::string& text,Fits fits) {
    std::vector<std::string> pages;
    for(size_t start=0;start<text.size();) {
        auto page=next_page(text,start,fits);start+=page.size();pages.push_back(std::move(page));
    }
    if(pages.empty())pages.emplace_back();return pages;
}
}
