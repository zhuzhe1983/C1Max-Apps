// Native Cast V2 sender. Wire shape follows Google's Cast channel protobuf;
// only Default Media Receiver messages are emitted (no private mirror API).
#include "internal.hpp"
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/sha256.h>
#include <mbedtls/ssl.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace casting {
static const char *connection="urn:x-cast:com.google.cast.tp.connection",*heartbeat="urn:x-cast:com.google.cast.tp.heartbeat",*receiver_ns="urn:x-cast:com.google.cast.receiver",*media_ns="urn:x-cast:com.google.cast.media";
static void check_pin(const std::string&path,const std::string&id,const std::string&digest){
    Json pins=Json::object();std::ifstream file(path);if(file){std::string text(65537,'\0');file.read(text.data(),text.size());text.resize(file.gcount());if(text.size()>65536)throw std::runtime_error("接收器证书记录过大");pins=Json::parse(text,nullptr,false);if(!pins.is_object())throw std::runtime_error("接收器证书记录损坏");}
    if(pins.contains(id)&&pins[id]!=digest)throw std::runtime_error("接收器证书已改变；请在设置中忘记后重新连接");if(pins.contains(id))return;if(pins.size()>=64)throw std::runtime_error("已保存设备过多");pins[id]=digest;auto s=pins.dump();
    std::string tmp=path+".XXXXXX";std::vector<char> t(tmp.begin(),tmp.end());t.push_back(0);Fd fd(mkstemp(t.data()));if(fd.n<0)throw std::runtime_error("无法保存接收器证书");bool ok=true;for(size_t i=0;i<s.size();){auto n=write(fd.n,s.data()+i,s.size()-i);if(n<0&&errno==EINTR)continue;if(n<=0){ok=false;break;}i+=n;}if(fsync(fd.n))ok=false;fd.reset();if(ok&&rename(t.data(),path.c_str())==0)return;unlink(t.data());throw std::runtime_error("无法保存接收器证书");
}
class Cast final:public Receiver {
    mbedtls_net_context net{};mbedtls_entropy_context entropy{};mbedtls_ctr_drbg_context rng{};mbedtls_ssl_context ssl{};mbedtls_ssl_config config{};
    bool online=false;int request=0,media_id=0;int64_t ping_at=0;std::string transport,session;Json playback{{"state","idle"}},receiver_status;std::vector<uint8_t> input;
    void tls_wait(int code,int64_t end){if(code==MBEDTLS_ERR_SSL_WANT_READ)wait_fd(net.fd,POLLIN,end);else if(code==MBEDTLS_ERR_SSL_WANT_WRITE)wait_fd(net.fd,POLLOUT,end);else throw std::runtime_error("Cast TLS 连接失败");}
    void send(const std::string&ns,Json payload,const std::string&dest="receiver-0"){
        auto body=cast_packet("c1max-sender",dest,ns,payload);uint32_t n=body.size();std::vector<uint8_t> framed{uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)};framed.insert(framed.end(),body.begin(),body.end());size_t i=0;auto end=now_ms()+2500;
        while(i<framed.size()){int r=mbedtls_ssl_write(&ssl,framed.data()+i,framed.size()-i);if(r>0)i+=r;else tls_wait(r,end);}
    }
    void handle(const Json&packet){
        if(!packet.contains("payload")||!packet["payload"].is_object())return;const auto&p=packet["payload"];std::string ns=packet.value("namespace",std::string()),type=p.value("type",std::string());
        if(ns==heartbeat&&type=="PING"){send(heartbeat,{{"type","PONG"}});return;}
        if(type=="CLOSE"&&packet.value("source",std::string())==transport){media_id=0;transport.clear();session.clear();playback={{"state","idle"}};return;}
        if(ns==receiver_ns&&type=="RECEIVER_STATUS"){
            receiver_status=p.value("status",Json::object());
            if(!receiver_status.is_object())throw std::runtime_error("无效 Cast 状态");
            if(!transport.empty()){
                bool present=false;
                for(auto&a:receiver_status.value("applications",Json::array()))
                    if(a.value("transportId",std::string())==transport)present=true;
                if(!present){transport.clear();session.clear();media_id=0;playback={{"state","idle"}};}
            }
        }
        if(ns==media_ns&&type=="MEDIA_STATUS"&&packet.value("source",std::string())==transport){
            if(!p.contains("status")||!p["status"].is_array())return;auto rows=p["status"];if(rows.empty()){media_id=0;playback={{"state","idle"}};return;}auto s=rows[0];media_id=s.value("mediaSessionId",0);auto state=s.value("playerState",std::string());
            playback={{"state",state=="PLAYING"?"playing":state=="PAUSED"?"paused":state=="BUFFERING"?"buffering":"idle"},{"position",s.value("currentTime",0.0)}};
            if(s.contains("media")&&s["media"].is_object())playback["duration"]=s["media"].value("duration",0.0);
            if(state=="IDLE"&&s.value("idleReason",std::string())=="ERROR")throw std::runtime_error("电视无法播放此媒体格式或地址");
        }
        if(type=="LOAD_FAILED"||type=="LOAD_CANCELLED"||type=="INVALID_REQUEST"||type=="LAUNCH_ERROR")throw std::runtime_error("电视拒绝播放请求："+type+" / "+p.value("reason",std::string()).substr(0,100));
    }
    bool drain(int milliseconds){
        auto end=now_ms()+milliseconds;bool got=false;
        for(;;){
            while(input.size()>=4){uint32_t n=(uint32_t(input[0])<<24)|(uint32_t(input[1])<<16)|(uint32_t(input[2])<<8)|input[3];if(n>262144||n==0)throw std::runtime_error("Cast 消息过大");if(input.size()<n+4)break;std::vector<uint8_t>b(input.begin()+4,input.begin()+4+n);input.erase(input.begin(),input.begin()+4+n);handle(cast_decode(b));got=true;}
            if(now_ms()>=end)return got;
            if(!mbedtls_ssl_get_bytes_avail(&ssl)){pollfd p{net.fd,POLLIN,0};int r=::poll(&p,1,int(std::max<int64_t>(0,end-now_ms())));if(r<=0)return got;if(!(p.revents&POLLIN))throw std::runtime_error("Cast 连接已断开");}
            uint8_t b[4096];int n=mbedtls_ssl_read(&ssl,b,sizeof b);if(n>0){input.insert(input.end(),b,b+n);if(input.size()>270336)throw std::runtime_error("Cast 消息过大");}else if(n==MBEDTLS_ERR_SSL_WANT_READ||n==MBEDTLS_ERR_SSL_WANT_WRITE)continue;else throw std::runtime_error("Cast 连接已断开");
        }
    }
    void launch(){
        send(receiver_ns,{{"type","LAUNCH"},{"appId","CC1AD845"},{"requestId",++request}});auto end=now_ms()+10000;
        while(now_ms()<end){drain(200);if(receiver_status.contains("applications"))for(auto &a:receiver_status["applications"]){if(a.value("appId",std::string())!="CC1AD845")continue;transport=a.value("transportId",std::string());session=a.value("sessionId",std::string());if(!transport.empty()){send(connection,{{"type","CONNECT"},{"origin",Json::object()}},transport);return;}}}
        throw std::runtime_error("电视媒体接收器未启动");
    }
public:
    Cast(){mbedtls_net_init(&net);mbedtls_entropy_init(&entropy);mbedtls_ctr_drbg_init(&rng);mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);}
    ~Cast(){mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);mbedtls_ctr_drbg_free(&rng);mbedtls_entropy_free(&entropy);mbedtls_net_free(&net);}
    void connect(const Json&d,const std::string&directory)override{
        std::string personal="c1max-cast";if(mbedtls_ctr_drbg_seed(&rng,mbedtls_entropy_func,&entropy,(const uint8_t*)personal.data(),personal.size()))throw std::runtime_error("无法初始化投屏加密");
        if(mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT))throw std::runtime_error("无法初始化 Cast TLS");
        // Cast devices use their own certificate hierarchy, not HTTPS Web PKI.
        // Pin the certificate on first explicitly selected LAN connection.
        mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_NONE);mbedtls_ssl_conf_rng(&config,mbedtls_ctr_drbg_random,&rng);
        if(mbedtls_ssl_setup(&ssl,&config))throw std::runtime_error("无法初始化 Cast 会话");net.fd=tcp(d.at("host"),d.value("port",8009));mbedtls_ssl_set_bio(&ssl,&net,mbedtls_net_send,mbedtls_net_recv,nullptr);
        int r;auto end=now_ms()+4000;while((r=mbedtls_ssl_handshake(&ssl))!=0)tls_wait(r,end);
        auto cert=mbedtls_ssl_get_peer_cert(&ssl);if(!cert)throw std::runtime_error("接收器未提供证书");uint8_t sum[32];mbedtls_sha256_ret(cert->raw.p,cert->raw.len,sum,0);const char *hex="0123456789abcdef";std::string digest;for(auto b:sum){digest+=hex[b>>4];digest+=hex[b&15];}
        check_pin(directory+"/pins.json",d.at("id"),digest);
        send(connection,{{"type","CONNECT"},{"origin",Json::object()}});send(receiver_ns,{{"type","GET_STATUS"},{"requestId",++request}});
        end=now_ms()+4000;while(receiver_status.is_null()&&now_ms()<end)drain(100);
        if(receiver_status.is_null())throw std::runtime_error("电视未回应 Cast 状态请求");
        online=true;ping_at=now_ms();
    }
    void load(const Json&m)override{
        if(!media_url(m.value("url",std::string())))throw std::runtime_error("无效媒体地址");if(transport.empty())launch();
        std::string mime=m.value("mime",std::string("video/mp4"));Json metadata{{"metadataType",mime.rfind("audio/",0)==0?3:0},{"title",m.value("title",std::string("C1 Max"))},{"artist",m.value("artist",std::string())}};
        auto image=m.value("image",std::string());if(media_url(image))metadata["images"]=Json::array({{{"url",image}}});
        Json media{{"contentId",m.at("url")},{"contentType",mime},{"streamType",m.value("live",false)?"LIVE":"BUFFERED"},{"metadata",metadata}};
        Json p{{"type","LOAD"},{"requestId",++request},{"sessionId",session},{"autoplay",true},{"currentTime",m.value("position",0.0)},{"media",media}};
        send(media_ns,p,transport);media_id=0;playback={{"state","buffering"}};drain(100);
    }
    void command(const std::string&a,double value)override{
        if(a=="volume"){if(!std::isfinite(value))return;send(receiver_ns,{{"type","SET_VOLUME"},{"requestId",++request},{"volume",{{"level",std::clamp(value,0.0,1.0)}}}});receiver_status["volume"]["level"]=std::clamp(value,0.0,1.0);drain(60);return;}
        if(!media_id){if(a=="stop"){
            // LOAD may still be buffering without a mediaSessionId. Cancel our
            // receiver session too, otherwise a timed-out load could play later.
            if(!session.empty())send(receiver_ns,{{"type","STOP"},{"requestId",++request},{"sessionId",session}});
            transport.clear();session.clear();playback={{"state","idle"}};return;
        }throw std::runtime_error("电视媒体尚未准备好");}
        Json p{{"requestId",++request},{"mediaSessionId",media_id}};if(a=="play")p["type"]="PLAY";else if(a=="pause")p["type"]="PAUSE";else if(a=="stop")p["type"]="STOP";else if(a=="seek"){if(!std::isfinite(value)||value<0||value>7*86400)throw std::runtime_error("无效播放位置");p["type"]="SEEK";p["currentTime"]=value;}else throw std::runtime_error("未知投屏操作");send(media_ns,p,transport);drain(20);
    }
    Json poll()override{if(!online)throw std::runtime_error("Cast 未连接");if(now_ms()-ping_at>5000){send(heartbeat,{{"type","PING"}});send(receiver_ns,{{"type","GET_STATUS"},{"requestId",++request}});ping_at=now_ms();}
        if(!transport.empty())send(media_ns,{{"type","GET_STATUS"},{"requestId",++request}},transport);drain(30);
        auto out=playback;if(receiver_status.contains("volume"))out["volume"]=receiver_status["volume"].value("level",0.0);return out;}
};
std::unique_ptr<Receiver> cast_receiver(){return std::make_unique<Cast>();}
}
