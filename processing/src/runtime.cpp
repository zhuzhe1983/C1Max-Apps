#include "runtime.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
namespace sketch {
static int64_t now(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return int64_t(t.tv_sec)*1000+t.tv_nsec/1000000;}
Runtime::~Runtime(){if(ctx_)JS_FreeContext(ctx_);if(rt_)JS_FreeRuntime(rt_);}
int Runtime::interrupt(JSRuntime *,void *p){return now()>static_cast<Runtime*>(p)->deadline_;}
bool Runtime::result(JSValue value){
    bool ok=!JS_IsException(value);
    if(!ok){auto e=JS_GetException(ctx_);const char *s=JS_ToCString(ctx_,e);error=s?s:"JavaScript error";if(s)JS_FreeCString(ctx_,s);auto stack=JS_GetPropertyStr(ctx_,e,"stack");s=JS_ToCString(ctx_,stack);if(s&&std::string(s)!="undefined")error+="\n"+std::string(s);if(s)JS_FreeCString(ctx_,s);JS_FreeValue(ctx_,stack);JS_FreeValue(ctx_,e);}
    JS_FreeValue(ctx_,value);return ok;
}
bool Runtime::eval(const std::string &s,int ms){if(!ctx_)return false;deadline_=now()+ms;return result(JS_Eval(ctx_,s.c_str(),s.size(),"sketch.js",JS_EVAL_TYPE_GLOBAL));}
bool Runtime::load(const std::string &api,const std::string &source){
    if(ctx_)JS_FreeContext(ctx_);if(rt_)JS_FreeRuntime(rt_);ctx_=nullptr;rt_=nullptr;error.clear();mouse_down_=false;pixels.fill(0xff111827);
    rt_=JS_NewRuntime();if(!rt_){error="Cannot allocate runtime";return false;}
    JS_SetMemoryLimit(rt_,8*1024*1024);JS_SetMaxStackSize(rt_,256*1024);JS_SetInterruptHandler(rt_,interrupt,this);
    ctx_=JS_NewContext(rt_);if(!ctx_){error="Cannot allocate context";return false;}JS_SetContextOpaque(ctx_,this);
    auto global=JS_GetGlobalObject(ctx_);JS_SetPropertyStr(ctx_,global,"__draw",JS_NewCFunction(ctx_,draw,"__draw",1));JS_FreeValue(ctx_,global);
    return eval(api,500)&&eval(source,500)&&eval("if(typeof setup==='function')setup();",500);
}
bool Runtime::step(){return eval("__matrix=[1,0,0,1,0,0];__stack=[];if(__loop){frameCount++;if(typeof draw==='function')draw();}");}
bool Runtime::pointer(int x,int y,bool down){bool pressed=down&&!mouse_down_;mouse_down_=down;return eval("mouseX="+std::to_string(std::clamp(x,0,width-1))+";mouseY="+std::to_string(std::clamp(y,0,height-1))+";mouseIsPressed="+(down?"true":"false")+";"+(pressed?"if(typeof mousePressed==='function')mousePressed();":""));}
bool Runtime::key(char c){return eval("key=String.fromCharCode("+std::to_string((unsigned char)c)+");if(typeof keyPressed==='function')keyPressed();");}
size_t Runtime::memory()const{if(!rt_)return 0;JSMemoryUsage m;JS_ComputeMemoryUsage(rt_,&m);return m.memory_used_size;}
void Runtime::pixel(int x,int y,uint32_t c){
    if(x<0||y<0||x>=width||y>=height)return;uint32_t a=c>>24;auto &d=pixels[y*width+x];if(a==255){d=c;return;}if(!a)return;
    auto blend=[&](int shift){return ((((c>>shift)&255)*a+((d>>shift)&255)*(255-a))/255)<<shift;};
    d=0xff000000|blend(16)|blend(8)|blend(0);
}
void Runtime::line(double x,double y,double a,double b,uint32_t c){
    // Clip before rasterizing so a huge off-screen line cannot monopolize the CPU.
    double dx=a-x,dy=b-y,t0=0,t1=1;
    auto clip=[&](double p,double q){if(p==0)return q>=0;double r=q/p;if(p<0){if(r>t1)return false;t0=std::max(t0,r);}else{if(r<t0)return false;t1=std::min(t1,r);}return true;};
    if(!clip(-dx,x)||!clip(dx,width-1-x)||!clip(-dy,y)||!clip(dy,height-1-y))return;
    int x0=std::lround(x+t0*dx),y0=std::lround(y+t0*dy),x1=std::lround(x+t1*dx),y1=std::lround(y+t1*dy);
    int ix=std::abs(x1-x0),iy=-std::abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,err=ix+iy;
    for(;;){pixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e=err*2;if(e>=iy){err+=iy;x0+=sx;}if(e<=ix){err+=ix;y0+=sy;}}
}
JSValue Runtime::draw(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){
    auto *r=static_cast<Runtime*>(JS_GetContextOpaque(ctx));if(now()>r->deadline_)return JS_ThrowInternalError(ctx,"Drawing budget exceeded");
    double v[10]={};if(argc>10)return JS_ThrowTypeError(ctx,"Too many drawing arguments");
    for(int i=0;i<argc;i++){if(JS_ToFloat64(ctx,&v[i],argv[i])<0)return JS_EXCEPTION;if(!std::isfinite(v[i])||std::abs(v[i])>4294967295.0)return JS_ThrowRangeError(ctx,"Invalid drawing coordinate");}
    if(argc<1||v[0]<0||v[0]>3||std::floor(v[0])!=v[0])return JS_ThrowTypeError(ctx,"Unsupported drawing operation");
    int op=int(v[0]);int ci=op==0?1:op==3?7:5;
    if(argc!=ci+1||v[ci]<0||v[ci]>4294967295.0)return JS_ThrowRangeError(ctx,"Invalid drawing color or arguments");
    if(op==0&&argc==2){r->pixels.fill(0xff000000|uint32_t(v[1]));return JS_UNDEFINED;}
    if(op==1&&argc==6){r->line(v[1],v[2],v[3],v[4],uint32_t(v[5]));return JS_UNDEFINED;}
    if(op==2&&argc==6){
        double x=v[1],y=v[2],rx=std::abs(v[3])*.5,ry=std::abs(v[4])*.5;if(rx<.5||ry<.5)return JS_UNDEFINED;
        int left=int(std::clamp(std::floor(x-rx),0.0,double(width-1))),right=int(std::clamp(std::ceil(x+rx),0.0,double(width-1)));
        int top=int(std::clamp(std::floor(y-ry),0.0,double(height-1))),bottom=int(std::clamp(std::ceil(y+ry),0.0,double(height-1)));
        for(int j=top;j<=bottom;j++)for(int i=left;i<=right;i++)if((i-x)*(i-x)/(rx*rx)+(j-y)*(j-y)/(ry*ry)<=1)r->pixel(i,j,uint32_t(v[5]));return JS_UNDEFINED;
    }
    if(op==3&&argc==8){
        double x=v[1],y=v[2],a=v[3],b=v[4],p=v[5],q=v[6];double area=(a-x)*(q-y)-(b-y)*(p-x);if(std::abs(area)<.001)return JS_UNDEFINED;
        int l=int(std::clamp(std::floor(std::min({x,a,p})),0.0,double(width-1))),h=int(std::clamp(std::ceil(std::max({x,a,p})),0.0,double(width-1)));
        int t=int(std::clamp(std::floor(std::min({y,b,q})),0.0,double(height-1))),z=int(std::clamp(std::ceil(std::max({y,b,q})),0.0,double(height-1)));
        for(int j=t;j<=z;j++)for(int i=l;i<=h;i++){double u=((a-i)*(q-j)-(b-j)*(p-i))/area,w=((p-i)*(y-j)-(q-j)*(x-i))/area;if(u>=0&&w>=0&&u+w<=1)r->pixel(i,j,uint32_t(v[7]));}return JS_UNDEFINED;
    }
    return JS_ThrowTypeError(ctx,"Unsupported drawing operation");
}
}
