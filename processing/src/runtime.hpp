#pragma once
#include <array>
#include <cstdint>
#include <string>
extern "C" {
#include "quickjs.h"
}
namespace sketch {
class Runtime {
    JSRuntime *rt_=nullptr;
    JSContext *ctx_=nullptr;
    int64_t deadline_=0;
    bool mouse_down_=false;
    bool result(JSValue);
    static int interrupt(JSRuntime *,void *);
    static JSValue draw(JSContext *,JSValueConst,int,JSValueConst *);
public:
    static constexpr int width=400,height=145;
    std::array<uint32_t,width*height> pixels{};
    std::string error;
    Runtime()=default;
    ~Runtime();
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    bool load(const std::string &api,const std::string &source);
    bool eval(const std::string &source,int budget_ms=150);
    bool step();
    bool pointer(int x,int y,bool down);
    bool key(char);
    size_t memory() const;
    void pixel(int x,int y,uint32_t color);
    void line(double x,double y,double a,double b,uint32_t color);
};
}
