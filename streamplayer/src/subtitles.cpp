#include "subtitles.hpp"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>

namespace subtitles {
namespace {
[[noreturn]] void invalid(){throw std::runtime_error("Invalid or oversized subtitle data");}
struct Bytes {
    const uint8_t* p;size_t n,pos=0;
    unsigned get(){if(pos>=n)invalid();return p[pos++];}
    unsigned word(){unsigned a=get();return (a<<8)|get();}
    unsigned triple(){unsigned a=word();return (a<<8)|get();}
    uint32_t dword(){uint32_t a=word();return (a<<16)|word();}
    void skip(size_t count){if(count>n-pos)invalid();pos+=count;}
};
int64_t timestamp(std::string s){
    std::replace(s.begin(),s.end(),',','.');std::vector<std::string> parts;
    std::istringstream in(s);std::string p;while(std::getline(in,p,':'))parts.push_back(p);
    if(parts.size()!=2&&parts.size()!=3)invalid();
    int64_t time=0;
    for(size_t i=0;i<parts.size();++i){
        auto &part=parts[i];bool last=i+1==parts.size();auto dot=part.find('.');
        std::string whole=last?part.substr(0,dot):part;
        if(whole.empty()||whole.size()>3||whole.find_first_not_of("0123456789")!=std::string::npos)invalid();
        int value=std::stoi(whole);if(i&&value>=60)invalid();time=time*60+value;
        if(last){
            if(dot==std::string::npos||part.size()-dot!=4)invalid();auto ms=part.substr(dot+1);
            if(ms.find_first_not_of("0123456789")!=std::string::npos)invalid();
            time=time*10000000+std::stoi(ms)*10000;
        }
    }
    return time;
}
std::string plain(const std::string&s){
    std::string out;
    for(size_t i=0;i<s.size()&&out.size()<4096;){
        if(s[i]=='<'){auto end=s.find('>',i);if(end==std::string::npos)break;i=end+1;continue;}
        if(s[i]=='&'){
            auto end=s.find(';',i);
            if(end!=std::string::npos&&end-i<12){
                auto e=s.substr(i,end-i+1);std::string c;
                if(e=="&amp;")c="&";else if(e=="&lt;")c="<";else if(e=="&gt;")c=">";
                else if(e=="&quot;")c="\"";else if(e=="&apos;"||e=="&#39;")c="'";else if(e=="&nbsp;")c=" ";
                if(!c.empty()){out+=c;i=end+1;continue;}
            }
        }
        unsigned char c=s[i++];if(c>=32||c=='\n'||c=='\t')out+=char(c);
    }
    while(!out.empty()&&(out.back()=='\n'||out.back()==' '))out.pop_back();return out;
}
std::string lower(std::string s){for(char &c:s)if(c>='A'&&c<='Z')c+=32;return s;}
std::vector<uint8_t> unrle(const Object&o){
    if(o.width<=0||o.height<=0||size_t(o.width)*o.height>4194304||o.rle.size()!=o.expected)invalid();
    std::vector<uint8_t> pixels(size_t(o.width)*o.height,0);Bytes b{o.rle.data(),o.rle.size()};int x=0,y=0;
    while(b.pos<b.n&&y<o.height){
        unsigned color=b.get(),count=1;
        if(color==0){unsigned flags=b.get();count=flags&63;if(flags&64)count=(count<<8)|b.get();color=flags&128?b.get():0;
            if(!count){if(x!=o.width)invalid();x=0;++y;continue;}}
        if(count>unsigned(o.width-x))invalid();
        std::fill_n(pixels.begin()+size_t(y)*o.width+x,count,uint8_t(color));x+=int(count);
    }
    if(y!=o.height&&!(y==o.height-1&&x==o.width))invalid();return pixels;
}
}
bool pgs_codec(std::string c){c=lower(c);return c=="pgssub"||c=="pgs"||c=="hdmv_pgs_subtitle";}
bool text_codec(std::string c){c=lower(c);return c=="subrip"||c=="srt"||c=="webvtt"||c=="vtt"||c=="ass"||c=="ssa"||c=="mov_text"||c=="text";}
Window vtt(const std::string&body,int64_t start,int64_t end){
    if(body.size()>1048576||start<0||end<=start)invalid();Window result{start,end,{}};
    std::istringstream in(body);std::string line;Cue cue;bool active=false;
    auto finish=[&]{if(active){cue.text=plain(cue.text);if(!cue.text.empty()&&cue.end>start&&cue.start<end)result.cues.push_back(std::move(cue));cue={};active=false;if(result.cues.size()>512)invalid();}};
    while(std::getline(in,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.size()>8192)invalid();
        if(line.empty()){finish();continue;}
        auto arrow=line.find("-->");
        if(arrow!=std::string::npos){finish();std::istringstream a(line.substr(0,arrow)),b(line.substr(arrow+3));std::string sa,sb;a>>sa;b>>sb;
            cue.start=timestamp(sa);cue.end=timestamp(sb);if(cue.end<=cue.start)invalid();active=true;
        }else if(active){if(!cue.text.empty())cue.text+='\n';cue.text+=line;if(cue.text.size()>8192)invalid();}
    }
    finish();std::stable_sort(result.cues.begin(),result.cues.end(),[](const Cue&a,const Cue&b){return a.start<b.start;});return result;
}
Window pgs(const std::string&body,int64_t start,int64_t end){
    if(body.size()>1048576||start<0||end<=start)invalid();Window result{start,end,{}};
    Bytes file{reinterpret_cast<const uint8_t*>(body.data()),body.size()};
    std::map<unsigned,std::shared_ptr<Object>> objects;std::map<unsigned,std::array<uint32_t,256>> palettes;
    struct Ref{unsigned id;int x,y,cx=0,cy=0,cw=0,ch=0;};std::vector<Ref> refs;
    int canvas_w=0,canvas_h=0;unsigned palette=0;int64_t pts=0;bool composition=false;
    size_t retained=0;
    while(file.pos<file.n){
        if(file.get()!='P'||file.get()!='G')invalid();uint32_t stamp=file.dword();file.skip(4);unsigned type=file.get(),length=file.word();
        if(length>file.n-file.pos)invalid();Bytes b{file.p+file.pos,length};file.skip(length);
        if(type==0x16){
            canvas_w=int(b.word());canvas_h=int(b.word());if(canvas_w<=0||canvas_h<=0||canvas_w>4096||canvas_h>2160)invalid();
            b.skip(3);unsigned state=b.get();if(state&0xc0){objects.clear();palettes.clear();}
            b.skip(1);palette=b.get();unsigned count=b.get();if(count>8)invalid();refs.clear();
            for(unsigned i=0;i<count;++i){Ref r{b.word(),0,0};b.skip(1);unsigned flags=b.get();r.x=int(b.word());r.y=int(b.word());
                if(flags&0x80){r.cx=int(b.word());r.cy=int(b.word());r.cw=int(b.word());r.ch=int(b.word());}
                refs.push_back(r);}
            pts=int64_t(stamp)*10000000/90000;composition=true;
        }else if(type==0x14){
            unsigned id=b.get();b.skip(1);if(palettes.size()>=16&&!palettes.count(id))invalid();auto&colors=palettes[id];
            while(b.pos<b.n){unsigned idx=b.get();int y=int(b.get())-16,cr=int(b.get())-128,cb=int(b.get())-128;unsigned alpha=b.get();
                // Limited-range YCbCr; HD palettes use BT.709, SD uses BT.601.
                auto clamp=[](int v){return unsigned(std::clamp(v,0,255));};
                int r=(298*y+(canvas_h>576?459:409)*cr+128)>>8;
                int g=(298*y-(canvas_h>576?55:100)*cb-(canvas_h>576?136:208)*cr+128)>>8;
                int blue=(298*y+(canvas_h>576?541:516)*cb+128)>>8;
                colors[idx]=(alpha<<24)|(clamp(r)<<16)|(clamp(g)<<8)|clamp(blue);
            }
        }else if(type==0x15){
            unsigned id=b.word();b.skip(1);unsigned flags=b.get();
            if(flags&0x80){unsigned size=b.triple();if(size<4||size>1048576)invalid();auto o=std::make_shared<Object>();o->expected=size-4;o->width=int(b.word());o->height=int(b.word());
                if(o->width<=0||o->height<=0||o->width>canvas_w||o->height>canvas_h||size_t(o->width)*o->height>4194304||objects.size()>=64)invalid();
                retained+=o->expected;if(retained>4194304)invalid();objects[id]=std::move(o);
            }
            auto it=objects.find(id);if(it==objects.end())invalid();auto&o=*it->second;
            if(b.n-b.pos>o.expected-o.rle.size())invalid();o.rle.insert(o.rle.end(),b.p+b.pos,b.p+b.n);
        }else if(type==0x80&&composition){
            composition=false;if(!result.cues.empty())result.cues.back().end=std::min(result.cues.back().end,pts);
            if(refs.empty())continue;
            auto pal=palettes.find(palette);if(pal==palettes.end())invalid();Cue cue;cue.start=pts;cue.end=end;cue.palette=pal->second;
            for(auto&r:refs){auto obj=objects.find(r.id);if(obj==objects.end()||obj->second->rle.size()!=obj->second->expected)invalid();auto&o=*obj->second;
                int cw=r.cw?r.cw:o.width,ch=r.ch?r.ch:o.height;
                if(r.cx+cw>o.width||r.cy+ch>o.height||r.x+cw>canvas_w||r.y+ch>canvas_h)invalid();
                cue.objects.push_back({obj->second,r.x,r.y,r.cx,r.cy,cw,ch});}
            if(cue.start<end){result.cues.push_back(std::move(cue));if(result.cues.size()>512)invalid();}
        }
    }
    if(composition)invalid();return result;
}
std::string text_at(const Window&w,int64_t ticks){
    std::string s;for(const auto&c:w.cues)if(c.start<=ticks&&ticks<c.end&&!c.text.empty()){
        if(!s.empty())s+='\n';s+=c.text;if(s.size()>8192)break;}
    return s;
}
const Cue* bitmap_at(const Window&w,int64_t ticks){
    for(auto it=w.cues.rbegin();it!=w.cues.rend();++it)if(it->start<=ticks&&ticks<it->end&&!it->objects.empty())return &*it;return nullptr;
}
Image bitmap(const Cue&c,int font_size){
    struct Decoded{const Placement*p;std::vector<uint8_t> index;};std::vector<Decoded> decoded;
    int left=4096,top=2160,right=-1,bottom=-1;size_t pixels=0;
    for(const auto&p:c.objects){pixels+=size_t(p.object->width)*p.object->height;if(pixels>4194304)invalid();auto bits=unrle(*p.object);
        for(int y=0;y<p.crop_h;++y)for(int x=0;x<p.crop_w;++x){auto color=c.palette[bits[size_t(y+p.crop_y)*p.object->width+x+p.crop_x]];
            if(color>>24){left=std::min(left,p.x+x);right=std::max(right,p.x+x);top=std::min(top,p.y+y);bottom=std::max(bottom,p.y+y);}}
        decoded.push_back({&p,std::move(bits)});
    }
    if(right<left||bottom<top)return {};
    int width=right-left+1,height=bottom-top+1;std::vector<bool> ink(size_t(height),false);
    for(auto&d:decoded){auto&p=*d.p;for(int y=0;y<p.crop_h;++y){int row=p.y+y-top;if(row<0||row>=height)continue;
        for(int x=0;x<p.crop_w;++x)if((c.palette[d.index[size_t(y+p.crop_y)*p.object->width+x+p.crop_x]]>>24)>96){ink[row]=true;break;}}}
    int lines=0,last=-10;for(int y=0;y<height;++y)if(ink[y]){if(y-last>4)++lines;last=y;}lines=std::clamp(lines,1,3);
    double scale=std::min({752.0/width,112.0/height,double((std::clamp(font_size,24,40)+4)*lines)/height});
    Image out;out.width=std::max(1,int(std::lround(width*scale)));out.height=std::max(1,int(std::lround(height*scale)));out.pixels.resize(size_t(out.width)*out.height);
    for(int y=0;y<out.height;++y)for(int x=0;x<out.width;++x){
        int sx=left+x*width/out.width,sy=top+y*height/out.height;uint32_t pixel=0;
        for(auto&d:decoded){auto&p=*d.p;int dx=sx-p.x,dy=sy-p.y;if(dx<0||dy<0||dx>=p.crop_w||dy>=p.crop_h)continue;
            auto color=c.palette[d.index[size_t(dy+p.crop_y)*p.object->width+dx+p.crop_x]];if((color>>24)>=(pixel>>24))pixel=color;}
        out.pixels[size_t(y)*out.width+x]=pixel;
    }
    return out;
}
}
