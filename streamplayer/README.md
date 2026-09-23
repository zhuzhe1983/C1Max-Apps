# C1Max StreamPlayer

从 `~/Workspace/m5stack/CardputerZero/StreamPlayer`（参考提交 `03bccbb58c229b03e8318277400b3d0253f57d70`）迁移 Emby/Jellyfin 账户、媒体库、PlaybackInfo 协商及会话上报流程。C1 Max 版重新实现 800×340 触屏界面与 MIPS 播放后端，不使用 CardputerZero 的 systemd、DRM、GStreamer 或 AArch64 包。

功能：服务器地址/账户输入、物理键盘与键帽 Shift 组合、Emby/Jellyfin 选择、登录令牌保存、媒体库、分页影片列表、中文标题，以及可隐藏的播放控制条。

- 登录密码仅发送给所填服务器，不落盘；成功后保存令牌和用户 ID 到 `/storage/apps/data/streamplayer/config.json`，权限 0600。
- HTTP/HTTPS 地址均可使用，反向代理子路径须写入地址。HTTPS 需要正常系统时间和受信任证书，未关闭证书验证。
- 服务端转码为 H.264 baseline / AAC，最大 400×170、20 fps，视频约 400 kbps、音频 64 kbps。默认保持宽高比填满 800 像素宽度，上下居中裁切至 340 像素；可切换 Fit 完整画面。超宽视频填宽后不足屏幕高度时保留上下黑边。
- 原厂 MPlayer 1.4 没有 yuv4mpeg 输出，PNG 输出也缺少编码器。设备后端使用私有 `c1max-yuv-pipe.so` 读取 FFmpeg 解码后的完整 YUV420 帧，经私有 FIFO 交给应用；MPlayer 使用 `-vo null -ao media`，不打开 framebuffer。适配器仅注入播放器子进程，原系统二进制和库不作修改；严格匹配 stock FFmpeg 4.2 / libavutil 56.31.100 与 O32/FP64/NaN2008 ABI。若播放器自带 yuv4mpeg，则直接使用该输出。
- 2026-09-23 真机静音测试已显示连续视频，约 20 fps，控制条、长暂停恢复与填宽/Fit均有截图和帧计数证据。解码帧在原播放器最终呈现等待前取出，因此音画同步仍需有声实测；本轮按用户要求禁用音频输出，没有据此声称音画同步已通过。
- 原厂 MPlayer 的 HLS 嵌套分段打开失败，因此使用 `/Videos/{id}/stream.ts` 连续 MPEG-TS 转码端点。服务器账号必须有转码权限。
- 网络请求有超时和 1 MB 响应上限。播放缓冲 2 MB；60 秒未获得有效进度、或有进度但 15 秒无视频帧且非暂停时结束，避免长期只有声音。开始/进度/停止向服务器报告，退出请求终止对应转码。
- 播放 URL 只写临时私有播放列表，不写入命令参数或普通日志，退出时清理。

服务端配置在设备输入即可；本仓库不携带私人服务器、账号、密码或 Token。可用 `C1_APPS_ROOT` / `C1_APPS_DATA` 覆盖路径进行测试。

播放时点击画面显示/隐藏控制条，播放中闲置 5 秒自动隐藏，暂停时保留。控制条提供暂停/继续、时间/总时长、拖动进度、前后 10 秒、静音、音量、填宽/完整画面和停止。确认键或空格暂停，A/D 前后 10 秒，Q/E 前后 1 分钟，H 显隐，F 切换画面，V 静音，返回停止。

进度跳转会停止当前转码，通过 `StartTimeTicks` 从目标时间重新请求流，通常有短暂缓冲；切换填宽/Fit 直接重绘已有完整帧，不重开网络流，暂停时也生效。暂停状态在跳转后保留。没有提供总时长的流禁用进度拖动。

暂未迁移海报缓存、搜索、字幕选择、音轨切换、倍速、蓝牙/HDMI 和 CardputerZero 系统调优。Emby 已用实际服务器进行本轮静音真机验证；Jellyfin 复用兼容 API，尚需独立 Jellyfin 服务器联测。

API 参考：[Emby Video Streaming](https://dev.emby.media/doc/restapi/Video-Streaming.html)。

物理键盘映射见 [键盘说明](../docs-keyboard.md)：双击 Shift 切换大小写，Shift 组合保留键帽符号；电源回 launcher，中间返回上一级，不删除文本。

显示层是唯一 framebuffer 写入者，在非扫描页完成视频与控件后一次 PAN。FIFO 按协议组装完整帧，分段读不会暴露半帧；坏格式、过大画面、有声无帧明确停止。最多 512×288 YUV420，UI 缓冲约 1 MiB，低分辨率 RGB/YUV 缓冲有上限，不用整片文件缓存。控制条刷新和暂停沿用最后完整画面。

旧版两个进程分别写第 0 页、拷贝并合成第 1/2 页的方案已移除：用户实测仍有破碎，之前静态截图不足以证明连续播放稳定。

本机 `tests/run.sh` 覆盖 TS 时间线、按字节碎片输入、完整帧发布、错误头/尺寸拒绝、填宽裁切/Fit、控制条显隐、旋转/步长与内存边界。它不能替代原装 MPlayer、硬件 PAN 时序、CPU 占用和实际 Emby 连续播放验证。接口依据：[MPlayer yuv4mpeg 输出说明](https://mplayerhq.hu/DOCS/man/en/mplayer.1.html)。

静音 QA 可在启动 launcher 时设置 `C1_STREAMPLAYER_SILENT=1`，这会使用 MPlayer `-ao null`，不会打开音频输出；该变量不写入用户配置。适配器源码和固定版本头文件在 `src/yuv_pipe.c` 与 `vendor/ffmpeg42`，CMake 调用 `tools/build-yuv-pipe.sh` 单独构建与原厂动态程序一致的 ABI。帧错误仅记录固定原因码，播放 URL 不输出日志。
