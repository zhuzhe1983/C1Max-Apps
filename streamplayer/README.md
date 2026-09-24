# C1Max StreamPlayer

从 `~/Workspace/m5stack/CardputerZero/StreamPlayer` 迁移 Emby/Jellyfin 的登录、媒体库和播放流程；C1 Max 使用原生 MIPS/LVGL 与原厂 MPlayer，不依赖 Java、浏览器或 GStreamer。

## 界面与操作

沿用 CardputerZero 的深色面板、Emby 绿 / Jellyfin 蓝、左侧媒体库、三张封面卡片和影片详情页，按 800×340 屏幕重新排布。每页只加载三张小封面，磁盘最多保留 60 张。使用实体键盘，没有虚拟键盘。

- 登录：URL、用户、密码可点击聚焦，Enter 到下一项；双击 Shift 切换大小写。
- 浏览：W/S 换媒体库，A/D 翻页，J/K 选卡片，Enter 进入详情并播放，中间返回键返回上一级。
- 播放：Enter/空格暂停，A/D 前后 10 秒，Q/E 前后 1 分钟，F 切换 Width/Fit，S 选择字幕，H 显隐控制条，V 静音；实体音量键可用，返回停止播放，电源回 launcher。
- 控制条提供进度、暂停、跳转、音量、画面模式、字幕和停止；暂停和字幕选择时不会自动隐藏。

## 分段转码与字幕

由应用读取 HLS 清单、下载完整 MPEG-TS 分段，通过本地 FIFO 交给同一个 MPlayer。原厂播放器不再直接打开 HLS 子链接，避免其 `mp:` 嵌套 URL 问题。请求约 3 秒一段，压缩段上限 1 MiB，限制预取距离；不缓存整部影片，也不启动第二个音频播放器。

- **Width** 请求 `MaxWidth=400, MaxHeight=288`，保留更多横向细节，再按比例填满 800 像素宽度、居中裁掉超出高度的内容。
- **Fit** 请求 `MaxWidth=400, MaxHeight=170`，显示完整画面。服务器保留片源比例；这两个参数是转码边界，不是把人物拉伸成屏幕比例，也不是服务端裁切滤镜。
- 切换模式时继续消费已缓冲的段，新设置从后续完整分段生效；新尺寸完整帧到达后才更改缩放布局。界面显示 `Next segment...`，快速连续操作合并为最后一次请求。新转码失败时保留旧设置；网络慢或字幕烧录启动慢仍可能短暂等待，不能保证零停顿。
- 字幕列表来自该片源的 `MediaStreams`，可关闭或选择语言/轨道。**字幕在本地独立叠加，不再烧进转码视频。** 文字轨道向服务器请求 WebVTT，使用本机 Noto Sans SC 绘制白字黑边；PGS 轨道请求原始 SUP，设备解码调色板/RLE 图片、裁去透明边并按字幕大小缩放。视频请求固定 `SubtitleStreamIndex=-1`，避免重复字幕。
- Width / Fit 都将字幕固定在屏幕底部安全区域，底部保留 14 像素；显示控制条时自动上移到控制条上方。默认文字字号 32，字幕面板 A− / A+ 调整到 24–40；PGS 调整图片大小，不进行 OCR，也不替换原字体。图片字幕长句受 752 像素宽度限制，文字最多显示 112 像素高的字幕区域。
- 字幕按源时间线加载约一分钟窗口，后台预取、最多保留两个窗口，每个响应不超过 1 MiB。PGS 保留压缩对象，仅解码当前字幕；单次解码像素有上限，避免整片蓝光字幕占满内存。切换轨道不重启视频转码；暂停、跳转、关闭和换片会正确保留或清理字幕状态。获取失败显示错误，不自动退回烧录字幕。
- 跳转重新请求播放，从包含目标时间的关键帧分段开始，可能比目标早不足一个分段；保留暂停状态。退出、跳转和替换转码时释放对应服务端编码会话。

请求 H.264/AAC、约 400 kbps 视频 + 64 kbps 音频、20 fps 上限；服务端是否遵循 profile/fps 参数取决于实现，实测 HLS 片源也可能输出约 24 fps。解码器严格拒绝超过 512×288、隔行或非 YUV420 的画面。

## 显示与资源

`c1max-yuv-pipe.so` 从原厂 FFmpeg 解码结果取得完整 YUV420 帧，MPlayer 使用 `-vo null`。只有应用写 framebuffer，将视频与 LVGL 控件合成到非扫描页后一次 PAN；分辨率改变通过新的帧头传递，不发布半帧。适配器严格匹配 libavutil 56.31.100 与 O32/FP64/NaN2008 ABI；不修改原系统二进制/库。

原厂 MPlayer 在调用解码器前丢弃包时间戳，界面在这种情况下使用其 slave 时间线与新帧尺寸判断模式切换。帧是在 MPlayer 最终呈现等待之前提取的，静音画面测试不代表音画同步通过有声验收。

目前分段入口支持连续时间线的 MPEG-TS VOD；加密 HLS、fMP4、带 discontinuity 的流和动态直播清单会明确报错。文字字幕兼容服务器能够转换为 WebVTT 的 SRT/ASS/SSA/WebVTT 等轨道；不保留 ASS 动画/复杂定位。图片字幕目前支持 PGS，未实现 DVD VobSub/DVB。字幕已嵌入原始画面的硬字幕无法移除。Jellyfin 使用兼容 API，仍需独立服务器联测。CardputerZero 的 HDMI、蓝牙、局域网扫描、搜索、剧集展开、音轨切换和倍速未在此版完整迁移。

## 配置与验证

配置位于 `/storage/apps/data/streamplayer/config.json`，登录后只保存令牌/用户 ID，权限 0600；登录配置不保存密码。可选的本地默认服务器（含自行填写的密码）见 [私有默认配置](../config/README.md)，它在 Git 忽略文件中维护、单独部署，不进入公开应用包；已保存的登录配置优先。仓库不携带账号、地址、Token 或影视内容。HTTP/HTTPS 均可使用，HTTPS 校验证书，服务器反向代理子路径须包含在地址中。网络请求有超时/取消与 1 MiB 响应上限；转码 URL 不写普通日志。

`tests/run.sh` 覆盖分段清单与 URL、尺寸边界、无效/加密流拒绝、分辨率变化时的碎片帧输入、完整帧发布、填宽/Fit、字幕面板合成、TS 时间线与暂停，以及 VTT 时间/重叠/标签、PGS 调色板/RLE/清除/损坏数据和字幕安全区域。设备验证记录见 [VALIDATION.md](../VALIDATION.md)。

设置 `C1_STREAMPLAYER_SILENT=1` 可临时用 MPlayer `-ao null` 进行静音 QA，不保存为用户配置。`C1_APPS_ROOT` / `C1_APPS_DATA` 支持独立测试目录；launcher supervisor 会覆盖这些变量，测试应在应用包装脚本中设置。

接口依据：[Emby Video Streaming](https://dev.emby.media/doc/restapi/Video-Streaming.html)、[转码尺寸及字幕参数](https://dev.emby.media/reference/RestAPI/VideoService/getVideosByIdStreamByContainer.html)。键盘映射见 [键盘说明](../docs-keyboard.md)。

独立字幕接口：[Emby SubtitleService](https://dev.emby.media/reference/RestAPI/SubtitleService/getVideosByIdByMediasourceidSubtitlesByIndexByStartpositionticksStreamByFormat.html)，使用 `StartPositionTicks`、`EndPositionTicks` 与 `CopyTimestamps=true`。SUP/PGS 字段与调色板格式参照 [FFmpeg 4.2 SUP demuxer](https://github.com/FFmpeg/FFmpeg/blob/n4.2/libavformat/supdec.c) 和 [PGS decoder](https://github.com/FFmpeg/FFmpeg/blob/n4.2/libavcodec/pgssubdec.c)，本地实现设有独立的帧、对象、字幕和内存边界。
