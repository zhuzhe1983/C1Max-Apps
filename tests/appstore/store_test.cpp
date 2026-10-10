#include "store.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
int main(int argc,char**argv){assert(argc==4);try{
    std::string root=argv[1],catalog=argv[2],bundles=argv[3];fs::create_directories(root+"/data");fs::create_directories(root+"/base");setenv("C1_APPS_DATA",(root+"/data").c_str(),1);setenv("C1_STORE_BASE",(root+"/base").c_str(),1);
    auto pub=Json::parse(c1::read_file(catalog));auto a=pub.at("apps").at(0);store::App app{a["id"],a["title"],a["description"],a["version"],a["revision"],a["url"],a["sha256"],a["size"],a["unpacked"]};
    Json local={{"schema",1},{"platform","c1max-mipsel-linux"},{"apps",Json::array()}};
    for(auto id:{app.id,std::string("appstore")}){fs::create_directories(root+"/base/"+id);auto exe=root+"/base/"+id+"/c1max-"+id;c1::save_private(exe,"test");chmod(exe.c_str(),0755);local["apps"].push_back({{"id",id},{"version","0.1.0"},{"revision",std::string(64,'a')}});}
    c1::save_private(root+"/base/catalog.json",local.dump());auto before=store::load_state();assert(before.at(app.id).installed);
    store::set_visible(app.id,false);auto state=store::load_state();assert(state.at(app.id).installed&&!state.at(app.id).visible);assert(c1::read_file(root+"/data/appstore/current/launcher-menu.txt").find("/"+app.id+"/")==std::string::npos);
    c1::save_private(root+"/base/"+app.id+"/manifest.json",Json{{"id",app.id},{"version","99.0.0"},{"revision",std::string(64,'b')}}.dump());
    auto deployed=store::load_state().at(app.id);assert(!deployed.visible&&deployed.version=="99.0.0"&&deployed.revision==std::string(64,'b'));
    assert(store::compare(deployed,app)=="本地版本较新");
    store::set_visible(app.id,true);assert(store::load_state().at(app.id).visible);
    store::rollback();assert(!store::load_state().at(app.id).visible);
    store::uninstall(app.id);assert(!store::load_state().at(app.id).installed);
    bool rejected=false;try{store::uninstall("appstore");}catch(...){rejected=true;}assert(rejected);
    fs::create_directories(root+"/unpack");auto bundle=bundles+"/"+fs::path(app.url).filename().string();store::unpack(bundle,root+"/unpack",app);assert(fs::exists(root+"/unpack/"+app.id+"/manifest.json"));
    auto bad=app;bad.sha256=std::string(64,'0');rejected=false;try{store::unpack(bundle,root+"/bad",bad);}catch(...){rejected=true;}assert(rejected);
    assert(store::compare({true,true,"99.0.0",std::string(64,'a'),""},app)=="本地版本较新");
    assert(store::compare({true,true,app.version,std::string(64,'a'),""},app)=="同版本，构建不同");
    std::cout<<"PASS selected visibility, independent uninstall, rollback, protected Store, package hashes and version ordering\n";return 0;
}catch(const std::exception&e){std::cout<<e.what()<<'\n';return 1;}}
