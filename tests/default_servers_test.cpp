#include "default_servers.hpp"
#include "client.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <unistd.h>
int main(){
    char pattern[]="/tmp/c1max-defaults-test-XXXXXX";auto dir=mkdtemp(pattern);assert(dir);setenv("C1_APPS_DATA",dir,1);
    struct Cleanup{std::string dir;~Cleanup(){std::filesystem::remove_all(dir);}} cleanup{dir};
    std::filesystem::create_directory(std::string(dir)+"/streamplayer");
    auto path=std::string(dir)+"/default-servers.json";
    assert(c1::default_server("streamplayer").empty());
    Json values={{"schema",1},{"streamplayer",{{"base","https://media.example.test/emby/"},{"username","fixture"},{"password","fixture-password"},{"type","Jellyfin"},{"token","must-not-import"}}},{"crosspoint",{{"name","Books"},{"url","https://books.example.test/opds"},{"user",""},{"password",""}}}};
    c1::save_private(path,values.dump());MediaClient client;client.load();
    assert(!client.ready()&&client.config["base"]=="https://media.example.test/emby"&&client.config["password"]=="fixture-password"&&!client.config.contains("token"));
    assert(c1::default_server("crosspoint")["url"]=="https://books.example.test/opds");
    c1::save_private(std::string(dir)+"/streamplayer/config.json",Json{{"base","https://saved.example.test"},{"username","saved"},{"type","Emby"},{"token","fixture-token"},{"user_id","fixture-id"}}.dump());
    client.load();assert(client.ready()&&client.config["base"]=="https://saved.example.test"&&!client.config.contains("password"));
    for(auto bad: {Json::array(),Json{{"schema",2}},Json{{"schema",1},{"streamplayer",{{"base",15}}}},Json{{"schema",1},{"streamplayer",{{"base","file:///etc/passwd"}}}},Json{{"schema",1},{"streamplayer",{{"base",std::string(2049,'a')}}}}}){
        c1::save_private(path,bad.dump());assert(c1::default_server("streamplayer").empty());
    }
    std::cout<<"Private defaults, no token import, saved settings precedence and malformed config checks passed\n";
}
