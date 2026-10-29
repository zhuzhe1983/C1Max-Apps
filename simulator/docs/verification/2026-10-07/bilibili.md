# Bilibili 403 修复与原始 MIPS 视频播放验证

在 macOS Apple Silicon、QEMU Malta/24Kf、128 MB MIPS Linux 中执行。官方 Bilibili 0.2.0 包及 ELF 保持原样，客体 SHA-256 与官方包一致。修改的是模拟器的 MPlayer 适配器和解码运行时。具体结果见 [bilibili-results.json](bilibili-results.json)。

## 原因和修复

原应用已经传入 `-user-agent Mozilla/5.0`、`-referrer https://www.bilibili.com/` 以及对应的 `-lavfstreamopts`，此前模拟器只取播放地址，没有把请求头转交给 FFmpeg。播放器请求因此被 CDN 拒绝，原应用显示“视频服务器拒绝访问（403），请返回后重试”。

同一条匿名 API 取得的签名视频地址执行 Range 请求，结果如下；签名地址没有进入版本库。

| 请求 | HTTP 状态 |
| --- | --- |
| HTTP，未传上述两个头 | 403 |
| HTTP，传上述两个头 | 206 |
| HTTPS，传上述两个头 | 206 |

修复将原参数转成 FFmpeg 的 HTTP `headers` 选项，协议说明见 [FFmpeg HTTP 文档](https://ffmpeg.org/ffmpeg-protocols.html#http)。没有修改原应用的 HTTP/HTTPS 策略，也没有传送登录 Cookie 到播放器。

`scripts/mplayer_headers_test.py` 通过真实 MIPS `/usr/bin/mplayer` 向本机校验请求头的服务播放自有 MP3：修复前两次返回 403、客体退出 1；修复后返回 200，两个头均正确，客体退出 0。最终镜像还通过了显式参数、仅备用参数、备用参数先出现三种调用方式，没有重复请求头。

## 视频、音频和控制

此前适配器只有音频输出。新增 `/usr/bin/c1max-video`，在 MIPS 客体内用 FFmpeg 6.1.5 解码 MP4 H.264/AAC；经原应用创建的 FIFO 输出 `YUV4MPEG2`、`FRAME Xpts=` 和 YUV420 数据。原应用负责 RGB 转换、控件和 framebuffer 呈现；AAC 经客体 OSS/AC97 输出，电脑没有代替客体解码视频。

`scripts/bilibili_video_test.py` 在独立盘安装官方包，以原应用已有的 `C1_BILI_QA_LOCAL` 模式播放自有 640×360、10 fps、30 秒的 H.264/AAC 视频。账号目录使用 `/tmp/c1-bili-qa`，与日常账号数据隔离。

| 阶段 | 原应用累计帧数 | 播放位置 | 暂停 |
| --- | ---: | ---: | --- |
| 播放 | 16 | 1.3 s | 否 |
| 暂停 | 25 | 2.4 s | 是 |
| 暂停中向前跳转 | 26 | 12.4 s | 是 |
| 恢复播放 | 44 | 14.2 s | 否 |
| 向后跳转 | 65 | 5.8 s | 否 |

测试还断言视频中央像素在播放时变化、暂停后稳定，以及暂停跳转确实产生新帧。[播放画面](bilibili-playing.png) 和 [暂停跳转画面](bilibili-paused-seek.png) 来自真实 guest framebuffer。

QEMU 声卡录音为 48 kHz 立体声，493,920 帧，其中 963,295 个样本非零；频率检测确认含视频中的 880 Hz 测试音。录音包含两轮测试和请求头测试，留在忽略的 `build/debug/bilibili/video-output.wav` 中，不是实时性测试。

日常实例重试原先报错的公开视频 `BV1XGpF6CEwq`，原应用持续接收到线上帧：20 秒时累计 56 帧、位置 2.2 秒；225.8 秒时累计 986 帧、位置 39.32 秒，并取得实际视频画面。请求头修复已载入日常实例，构建镜像也包含修复，重启后仍有效。

## 范围

当前视频路径支持 MP4 中的 8-bit、逐行、偶数尺寸 YUV420 H.264 和 AAC。输入尺寸上限遵循原应用：单边不超过 960，像素不超过 307,200；FIFO 输出缩放到不超过 400×288。纯音频路径保持原有播放控制能力。

软件解码在 QEMU 中较慢，本次验证证明了下载、解码、显示、声音和控制链路，不证明实时播放或硬件视频解码性能。没有验收其他编码、直播协议、会员及地区限制视频，也没有在 Windows/Linux 主机验收。
