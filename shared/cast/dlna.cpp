#include "internal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <thread>
#include <stdexcept>
namespace casting {
class Dlna final:public Receiver {
    Json device,media;std::string state="idle";double pending_seek=0;
    Http soap(const std::string&action,const std::string&args="",bool rendering=false){
        std::string service=device.at(rendering?"volume_service":"service"),body="<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:"+action+" xmlns:u=\""+xml_escape(service)+"\"><InstanceID>0</InstanceID>"+args+"</u:"+action+"></s:Body></s:Envelope>";
        auto r=http(device.at(rendering?"volume_control":"control"),"POST",body,{"Content-Type: text/xml; charset=\"utf-8\"","SOAPACTION: \""+service+"#"+action+"\""},10000);if(r.code!=200)throw std::runtime_error("DLNA "+action+" 失败（"+std::to_string(r.code)+"/"+xml_text(r.body,"errorCode")+"）："+xml_text(r.body,"errorDescription").substr(0,100));return r;
    }
public:
    void connect(const Json&d,const std::string&)override{device=d;soap("GetTransportInfo");}
    void load(const Json&m)override{
        std::string url=m.at("url"),mime=m.value("mime",std::string("video/mp4"));if(!media_url(url))throw std::runtime_error("无效媒体地址");
        std::string meta="<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"><item id=\"0\" parentID=\"-1\" restricted=\"1\"><dc:title>"+xml_escape(m.value("title",std::string()))+"</dc:title><upnp:class>object.item."+(mime.rfind("audio/",0)==0?"audioItem.musicTrack":"videoItem")+"</upnp:class>";
        auto image=m.value("image",std::string());if(media_url(image))meta+="<upnp:albumArtURI>"+xml_escape(image)+"</upnp:albumArtURI>";
        meta+="<res protocolInfo=\"http-get:*:"+xml_escape(mime)+":*\">"+xml_escape(url)+"</res></item></DIDL-Lite>";
        soap("SetAVTransportURI","<CurrentURI>"+xml_escape(url)+"</CurrentURI><CurrentURIMetaData>"+xml_escape(meta)+"</CurrentURIMetaData>");media=m;soap("Play","<Speed>1</Speed>");state="buffering";
        pending_seek=m.value("position",0.0);
    }
    void command(const std::string&a,double value)override{
        if(a=="play")soap("Play","<Speed>1</Speed>");else if(a=="pause")soap("Pause");else if(a=="stop")soap("Stop");
        else if(a=="seek"){if(!std::isfinite(value)||value<0||value>7*86400)throw std::runtime_error("无效播放位置");int s=value;char t[40];snprintf(t,sizeof t,"%02d:%02d:%02d",s/3600,s/60%60,s%60);auto seek=[&]{soap("Seek","<Unit>REL_TIME</Unit><Target>"+std::string(t)+"</Target>");};
            try{seek();}catch(const std::exception&){
                if(state!="paused")throw;
                // Kodi only accepts Seek while PLAYING. Preserve the caller's
                // paused state, including if the retry fails.
                soap("Play","<Speed>1</Speed>");
                try{auto end=now_ms()+1500;while(xml_text(soap("GetTransportInfo").body,"CurrentTransportState")!="PLAYING"){
                    if(now_ms()>end)throw std::runtime_error("接收端尚未准备好跳转");
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }seek();soap("Pause");}catch(...){try{soap("Pause");}catch(...){}throw;}
            }}
        else if(a=="volume"){
            if(!std::isfinite(value)||device.value("volume_control",std::string()).empty())throw std::runtime_error("接收端没有音量控制服务");
            soap("SetVolume","<Channel>Master</Channel><DesiredVolume>"+std::to_string(int(std::clamp(value,0.0,1.0)*100+.5))+"</DesiredVolume>",true);
        }else throw std::runtime_error("此 DLNA 设备未接入该操作");
    }
    Json poll()override{auto r=soap("GetTransportInfo");auto t=xml_text(r.body,"CurrentTransportState");state=t=="PLAYING"?"playing":t=="PAUSED_PLAYBACK"?"paused":t=="TRANSITIONING"?"buffering":"idle";Json s{{"state",state}};
        if(state=="playing"||state=="paused")try{auto p=soap("GetPositionInfo");auto parse=[](const std::string&v){int h=0,m=0;double s=0;return sscanf(v.c_str(),"%d:%d:%lf",&h,&m,&s)==3&&h>=0&&h<168&&m>=0&&m<60&&s>=0&&s<60?h*3600+m*60+s:0;};s["position"]=parse(xml_text(p.body,"RelTime"));s["duration"]=parse(xml_text(p.body,"TrackDuration"));}catch(...){}
        if(pending_seek>0&&state=="playing"&&s.value("duration",0.0)>0){double position=pending_seek;pending_seek=0;command("seek",position);s["position"]=position;}
        if(!device.value("volume_control",std::string()).empty())try{
            auto volume=soap("GetVolume","<Channel>Master</Channel>",true);
            int value=std::stoi(xml_text(volume.body,"CurrentVolume"));if(value>=0&&value<=100)s["volume"]=value/100.0;
        }catch(...){}return s;}
};
std::unique_ptr<Receiver> dlna_receiver(){return std::make_unique<Dlna>();}
}
