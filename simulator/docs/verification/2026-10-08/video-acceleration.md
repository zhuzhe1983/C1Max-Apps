# 原始 MIPS 应用的视频加速验证

macOS Apple Silicon，QEMU Malta/24Kf，128MB、Linux 6.6.60。Bilibili 0.2.0 官方 ELF 没有修改，客体中重新核对 SHA-256 为 `dd448002834cfb4567230372424c8223662f9832e425c8cb960e2aaa9c086565`。

## 测量与实现

使用同一段自有 H.264/AAC、640×360、10fps、30 秒测试视频，原始应用通过已有的 `C1_BILI_QA_LOCAL` 模式运行。以原应用的帧统计和播放位置衡量性能，1.0 倍速表示视频时间与墙钟时间一致。

| 路径 | 播放速度 | 实际接收帧率 |
| --- | ---: | ---: |
| 原 MIPS 客体解码，两轮基线 | 0.360、0.380 倍速 | 约 3.6～3.8fps |
| 主机解码，完整控制回归 | 1.042 倍速 | 9.982fps |
| 加快屏幕采集后的最终控制回归 | 1.006 倍速 | 9.960fps |

短窗口的位置查询存在采样误差；结果说明已经达到实时播放，不表示有意加速视频。最终镜像的完整统计见 [video-results.json](video-results.json)。

主要瓶颈是 QEMU 中单线程、纯 C 的 MIPS H.264 解码。主机原生 FFmpeg 解码并缩放完整 30 秒素材只需约 0.16 秒。默认路径改为主机 FFmpeg 生成交错的原始 YUV420 与 PCM，按单调时钟节奏发送；MIPS 客户端保持完整帧边界，将画面交回原应用的 Y4M FIFO，将 PCM 交给客体音量与 AC97。接收端只保留最新待显示帧，避免渲染积压导致慢速播放；画面上限 20fps。

`--video-backend guest` 保留原有客体 FFmpeg 路径。两条路径都不替换应用逻辑，也不模拟 X2000 VPU。视频伴音在加速模式中由主机解码，独立的纯音频播放仍由 MIPS 客体解码。

## 回归结果

实际原始 MIPS 应用通过了播放时画面变化、暂停画面稳定、暂停中向前跳转产生新帧、恢复播放和向后跳转；测试截图为 [播放画面](video-playing.png) 和 [暂停跳转](video-paused-seek.png)。

实际 QEMU AC97 录音为 48kHz、16 位立体声，948,345 帧、1,893,483 个非零样本；频率检测确认包含自有视频的 880Hz 测试音。结果见 [audio-results.json](audio-results.json)，录音保留在忽略的 `build/debug/bilibili/accelerated-final.wav`。

`scripts/video_service_test.py` 调用真实主机 FFmpeg/ffprobe，验证网络 Range、原始 User-Agent/Referer、403 错误识别、30 帧与完整 PCM 字节数、自然结束、精确到小数秒的暂停跳转、恢复、后退，以及停止后解码进程退出、临时上传文件删除。暂停/跳转终止旧进程时处理不完整 AVI 包；精确跳转后的空 AVI 时间槽也有回归覆盖。

`scripts/mplayer_headers_test.py` 通过真实 MIPS 播放器验证四种原参数组合，全部返回 200、退出 0，既有纯音频请求头修复保持正常。日常实例也取得线上 360p 视频画面；20～45 秒的原应用统计实际接收 17.463fps、播放速度 1.004 倍，见 [online-results.json](online-results.json)。此项是现场观察，独立测试盘的确定性结果见上述 JSON。

## 复现和范围

```sh
python3 scripts/video_service_test.py -v
python3 start.py --port 8776 --disk runtime/video-test.qcow2 --audio-record build/video-test.wav
python3 scripts/bilibili_video_test.py --url http://127.0.0.1:8776 --min-speed 0.8 --min-fps 8
python3 scripts/mplayer_headers_test.py --url http://127.0.0.1:8776
python3 start.py --stop --port 8776
```

录音文件必须不存在。网络输入沿用原应用的请求头和启动代理，经主机 urllib 校验证书；签名地址不写入主机解码进程参数或提交记录。客体本地视频上传为短期文件，限制 64MB，结束即删除。输入格式与旧路径一致：逐行、8-bit、偶数尺寸 YUV420 H.264，单边不超过 960、总像素不超过 307,200，输出不超过 400×288。

本次没有验收直播、其他编码或 Windows/Linux 主机性能；应用自身的 MIPS 绘制和屏幕传输仍影响最终可见帧率。
