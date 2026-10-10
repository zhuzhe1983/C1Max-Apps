# 无线投屏（媒体投送）

设置 0.3.0、StreamPlayer 0.4.0、Airtune 0.4.0、launcher 0.3.2。

## 使用

1. 词典和接收器连接同一局域网。Google TV 可用 Google Cast；其他电视可开启 AirScreen 等软件的 DLNA 接收。兼容性取决于接收器实际提供的服务。
2. 打开 **设置 → 无线投屏 → 搜索接收设备**，选择接收器。连接只建立控制会话，不会立刻抢占电视播放。
3. 返回 launcher，在 StreamPlayer 或 Airtune 中重新选择一项内容开始播放。已有本机播放不会被突然转移。电视直接从媒体服务器取流，词典不承担二次编码。
4. 设置页可调节电视音量、暂停／继续和断开；StreamPlayer 的电视播放页可跳转与切换字幕。实体音量键同步控制正在投屏的接收器，长按音量减三秒发送静音。关闭投屏后需重新选择本机播放，不会因网络断开突然打开词典扬声器。

StreamPlayer 为电视请求最高 1280×720、30fps 的 H.264/AAC HLS；实际尺寸不超过源视频，码率上限约 2.63Mbps。字幕选中后由 Emby/Jellyfin 烧录，按当前播放位置重开电视媒体会话。本机低分辨率／本地字幕路径保持独立。

Airtune 和 StreamPlayer 音乐通过共享音频服务发送音频地址及曲目名称。后台开关仍由各应用控制。海报／文字排版由接收端决定；这里不强制自定义电视界面。

## 用 MacBook 接收

安装官方开源 [Kodi](https://kodi.tv/download/macos/)，在其 **Settings → Services → UPnP / DLNA** 中打开 **Enable UPnP support** 和 **Allow remote control via UPnP**。不需要分享本机媒体库。服务的设备名称可自行设置，如 `C1Max MacBook`。Kodi 运行时词典即可搜索并选择；退出 Kodi 后停止接收。该功能见 [Kodi 官方说明](https://kodi.wiki/view/Settings/Services/UPnP_DLNA)。

本次测试安装了 macOS ARM64 Kodi 21.3，使用窗口模式，未设置开机启动。它是媒体接收器，不能据此认定桌面或 NES 镜像已经实现。

## 当前边界

- **游戏／全桌面镜像不可用。** 硬件编码驱动探测期间设备发生重启，已停止探测并禁用这条路径。尚无游戏编码性能或投屏延迟的实测结果。
- **AirPlay / Apple TV 不可用。** 目前只发现并显示设备，不冒充已完成配对或镜像。
- **Google Cast 尚需电视端播放复测。** 真机成功建立 TLS 与 GET_STATUS，但现场接收器返回 `LAUNCH_ERROR / NOT_FOUND`。该错误表示默认媒体接收应用未启动，不能说已经开始传输视频。可改选提供 DLNA 的接收器。
- DLNA 接收器的媒体格式、HTTPS、HLS、跳转与字幕支持各有差别。Kodi 暂停时拒绝 Seek，发送端会在必要时短暂恢复、跳转，再保持暂停。
- 媒体 URL 必须能被接收端访问，包括地址中的鉴权参数。本地文件、需要特殊 HTTP 请求头的源和只对词典可见的服务没有通用代理回退。
- 局域网发现和控制目前使用 IPv4；VLAN／访客网络隔离、多播过滤会影响搜索。

## 实现与测试

`shared/cast/` 是原生 C++ 服务；`shared/cast_client.cpp` 是本地 IPC 接口。网络工作在服务线程执行，界面不等待电视网络。单一媒体拥有者绑定 PID 与进程启动时间，旧应用不能停止新应用的会话。应用退出或 launcher 会话结束时停止接收端；共享后台音频由音频服务继续持有。

本地目录 `data/cast` 为 0700，控制 socket 和凭据文件为 0600。Cast 自签名证书按用户主动选择的设备进行首次信任指纹固定，证书变化要求“忘记”后重连。此机制不等同于 Google 官方设备证书认证；普通 HTTPS 请求的校验没有关闭。未经用户选择不会自动连接接收器。

设置的单独安装包自带 `c1max-castd`，完整运行时也提供共享副本。首次投屏先安装／更新设置，再更新 StreamPlayer、Airtune；更新 launcher 运行时后全局音量与退出清理完整生效。

测试位于 `shared/cast/tests`：协议边界、真实 TLS 本地接收器、Linux IPC／SOAP 接收器、权限、证书变化、错误恢复、进程退出、音量与静音。它们不会访问真实电视。`protocol-probe --exercise` 是人工指定设备的真机测试工具，会实际播放媒体，请勿对未选择的接收器运行。
