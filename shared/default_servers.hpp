#pragma once
#include "net.hpp"

namespace c1 {
// Private defaults are deployed separately from the public application payload.
// They only seed an app without its own saved configuration; never import tokens.
inline Json default_server(const std::string& app) {
    try {
        auto all=Json::parse(read_file(data()+"/default-servers.json",16384));
        if(!all.is_object()||all.value("schema",0)!=1||!all.contains(app)||!all[app].is_object())return Json::object();
        if(app!="streamplayer"&&app!="crosspoint")return Json::object();
        auto fields=app=="streamplayer"?std::vector<std::string>{"base","username","password","type"}:
            std::vector<std::string>{"name","url","user","password"};
        Json out=Json::object();
        for(auto& field:fields)if(all[app].contains(field)){
            auto& value=all[app][field];if(!value.is_string()||value.get_ref<const std::string&>().size()>2048)return Json::object();
            out[field]=value;
        }
        auto url=out.value(app=="streamplayer"?"base":"url",std::string());
        if(url.empty())return Json::object();origin(url);
        if(app=="streamplayer"){
            while(!url.empty()&&url.back()=='/')url.pop_back();out["base"]=url;
            if(out.value("type",std::string("Emby"))!="Jellyfin")out["type"]="Emby";
        }
        return out;
    }catch(...){return Json::object();}
}
}
