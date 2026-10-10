// Linked only into the Linux IPC test daemon; production always uses mDNS/SSDP.
#include "../internal.hpp"
#include <cstdlib>
namespace casting {
std::vector<Json> discover(int) {
    auto port=getenv("C1_CAST_TEST_PORT");
    if(!port)return {};
    return {{{"id","dlna:fixture"},{"name","Test receiver"},{"protocol","dlna"},
             {"host","127.0.0.1"},{"port",std::stoi(port)},{"supported",true},
             {"service","urn:schemas-upnp-org:service:AVTransport:1"},
             {"volume_service","urn:schemas-upnp-org:service:RenderingControl:1"},
             {"volume_control","http://127.0.0.1:"+std::string(port)+"/control"},
             {"control","http://127.0.0.1:"+std::string(port)+"/control"}}};
}
}
