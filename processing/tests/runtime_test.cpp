#include "runtime.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <sys/resource.h>
#include <chrono>
static std::string read(const std::string &p){std::ifstream f(p);if(!f)throw std::runtime_error(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
    if(argc!=2){std::cerr<<"Usage: c1max-sketch-test processing-directory\n";return 2;}
    const std::string folder=argv[1],api=read(folder+"/api.js");
    for(auto name:{"tree","koch","flocking","particles"}){
        sketch::Runtime r;assert(r.load(api,read(folder+"/examples/"+name+".js")));
        auto start=std::chrono::steady_clock::now();
        for(int i=0;i<180;i++)if(!r.step()){std::cerr<<name<<": "<<r.error<<'\n';return 1;}
        auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
        assert(r.pointer(130,30,true));assert(r.key('a'));bool drawn=false;for(auto p:r.pixels)if(p!=r.pixels[0])drawn=true;assert(drawn);
        struct rusage u;getrusage(RUSAGE_SELF,&u);std::cout<<name<<" frames=180 ms="<<elapsed<<" JS_bytes="<<r.memory()<<" RSS_peak_kB="<<u.ru_maxrss<<'\n';
    }
    sketch::Runtime r;assert(!r.load(api,"while(true){}"));assert(r.load(api,"function draw(){while(true){}}"));assert(!r.step());
    assert(!r.load(api,"let x=new ArrayBuffer(32*1024*1024);"));
    assert(r.load(api,"function draw(){stroke(255);line(-1e8,72,1e8,72);}"));assert(r.step());assert(r.pixels[72*400]==0xffffffff);
    assert(r.load(api,"function draw(){ellipse(NaN,0,5);}"));assert(!r.step());
    assert(r.load(api,"function draw(){background(0);fill(200,100,50);noStroke();rect(10,10,20,20);}"));assert(r.step());assert((r.pixels[15*400+15]&0xffffff)==0xc86432);
    assert(r.load(api,"let count=0;function mousePressed(){count++;}"));assert(r.pointer(10,10,true));assert(r.pointer(20,20,true));assert(r.pointer(30,30,false));assert(r.pointer(40,40,true));assert(r.eval("if(count!==2)throw Error('Pointer edge');"));
    r.pixels.fill(0xff102030);r.pixel(0,0,0x804080c0);assert(r.pixels[0]==0xff285078);
    std::cout<<"PASS demo execution, timeout recovery, heap limit, clipping and raster colors\n";
}
