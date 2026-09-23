#include "client.hpp"
#include <cassert>
#include <iostream>
int main(int argc,char**argv){
 try{
  if(argc>1 && std::string(argv[1])=="--live"){
   MediaClient c;c.load();auto libs=c.libraries();std::cout<<"libraries="<<libs.size()<<std::endl;
   for(auto&l:libs){auto items=c.items(l.at("Id"));if(items.at("Items").empty())continue;auto item=items.at("Items").at(0);auto p=c.playback(item.at("Id"));
    c1::save_private(c1::data()+"/streamplayer/test-playback.m3u","#EXTM3U\n"+p.url+"\n");
    c1::save_private(c1::data()+"/streamplayer/test-session.json",Json{{"item",p.item},{"source",p.source},{"session",p.session}}.dump());
    std::cout<<"PlaybackInfo returned transcode URL; saved privately. Title="<<item.value("Name",std::string())<<std::endl;return 0;
   }return 2;
  }
  if(argc>1 && std::string(argv[1])=="--stop-test"){
   MediaClient c;c.load();auto j=Json::parse(c1::read_file(c1::data()+"/streamplayer/test-session.json"));Playback p;p.item=j["item"];p.source=j["source"];p.session=j["session"];c.stop_transcode(p);return 0;
  }
  auto app=Json{{"id","piano"},{"version","0.1.0"},{"revision",std::string(64,'a')}};
  auto local=Json{{"schema",1},{"platform","c1max-mipsel-linux"},{"apps",Json::array({app})}};
  assert(c1::check_updates(local,local)[0]["state"]=="current");auto remote=local;remote["apps"][0]["version"]="0.2.0";assert(c1::check_updates(local,remote)[0]["state"]=="update");
  remote=local;remote["apps"][0]["revision"]=std::string(64,'b');assert(c1::check_updates(local,remote)[0]["state"]=="changed");
  remote=local;remote["apps"][0]["version"]="0.0.9";assert(c1::check_updates(local,remote)[0]["state"]=="local newer");
  remote=local;remote["apps"][0]["id"]="nes";assert(c1::check_updates(local,remote)[0]["state"]=="new");
  for(auto bad:{"../bad","BAD",""}){remote=local;remote["apps"][0]["id"]=bad;bool threw=false;try{c1::check_updates(local,remote);}catch(...){threw=true;}assert(threw);}
  remote=local;remote["apps"].push_back(app);bool threw=false;try{c1::check_updates(local,remote);}catch(...){threw=true;}assert(threw);
  assert(c1::encode("a b&c")=="a%20b%26c");assert(c1::resolve("http://localhost/emby","/emby/a")=="http://localhost/emby/a");
  threw=false;try{c1::resolve("http://localhost","https://evil.example/a");}catch(...){threw=true;}assert(threw);
  std::cout<<"Catalog/version/URL validation passed\n";
 }catch(std::exception&e){std::cerr<<e.what()<<std::endl;return 1;}return 0;
}
