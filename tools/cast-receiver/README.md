# C1 Max 游戏投屏桥接（Mac）

NES、PCSX4all、DOSBox 使用同一输出设置：**仅本机 / 仅远端 / 双屏**。在 C1 Max 的「设置 → 无线投屏」选择接收端并为每个应用保存输出模式；设置在下一次开始游戏时生效，默认仅本机。

这一版需要 Mac 同时运行本桥接和 Kodi 接收端。C1 Max 把游戏区域的原始帧和 PCM 送到已配对的 Mac，由 Mac 编码为 H.264/AAC HLS；它不是设备独立的 Google Cast/AirPlay 游戏镜像。也没有访问曾导致设备重启的硬件编码器。

## 安装和配对

1. Mac 安装 Python 3、FFmpeg 和 Kodi，启用 Kodi 的 UPnP/DLNA renderer，将接收端命名为 `C1Max MacBook`。
2. 将下面的地址换成当前 Mac 的局域网 IPv4，序列号换成 `adb devices` 显示的 C1 Max：

   ```sh
   python3 tools/cast-receiver/pair.py --host 192.168.1.10 --serial YOUR_ADB_SERIAL
   python3 tools/cast-receiver/server.py
   ```

3. 保持服务和 Kodi 打开，在词典「无线投屏」中连接这台 Mac，再选择游戏的输出模式。

`pair.py` 生成 256 位随机私有令牌，写入 Mac 的 `~/Library/Application Support/C1MaxCast/config.json` 和词典的 `/storage/apps/data/cast/game-bridge.json`。文件权限为 `0600`；不写入源码仓库。再次执行保留令牌并更新 IP。Mac 地址改变后需重新配对并重新扫描接收端。

默认 TCP 控制/原始媒体端口为 `28766`，HLS HTTP 端口为 `28767`；服务只接受已配对客户端的固定媒体格式。没有任意文件读取、上传、代理任意 URL 或执行命令的接口。HLS 地址含每次会话独立的随机能力令牌，游戏结束就失效。连接是局域网明文传输，请只用于可信局域网，不向互联网映射这两个端口。

## 三种模式

| 模式 | 词典 | 接收端 |
|---|---|---|
| 仅本机 | 原有游戏画面与声音 | 不连接、不发送 |
| 仅远端 | 保留实体操作、按键提示和投屏状态 | 游戏画面与声音 |
| 双屏 | 保留原有游戏画面和实体操作 | 同步游戏画面与声音 |

远端真正进入播放状态后才隐藏本机画面／淡出本机声音；双屏也默认仅远端出声。短暂缓冲或暂停不会突然恢复本机出声。连接失败、接收端停止、Mac 服务退出时恢复本机并显示提示。暂停模拟器菜单会保持桥接，Mac 重复最后一帧、补静音，不重播积压的游戏帧。

网络故障时会先发送停止并关闭媒体生产，再恢复本机。若接收端已经失联，它可能还会短暂播放自身缓存；这种情况下无法保证远端立刻静音。

NES/PS1 远端显示比例为 4:3，不带本机两侧快捷键；DOSBox 4:3 填满传输画面，16:9 以正确比例上下留黑。采集直接来自模拟器有效帧，不读取整块 framebuffer。

## 帧率和延迟限制

当前桥接统一为 **320×240、目标 20 fps、RGB565LE、44.1 kHz 双声道**。网络约 3.1 MB/s 视频加 176 KB/s PCM；较慢的 Wi-Fi 会丢旧视频帧，绝不无限排队或降低模拟器原本的限速。PS1 高分辨率文字会被缩小到 320×240。仅远端减少本机绘制，但仍需要完整模拟和采集，不能保证提高游戏帧率。

**Kodi/DLNA 的 HLS 存在多秒缓冲，不适合依赖远端画面反应的动作游戏。** 双屏可用本机操作，远端作为观看画面。实际延迟取决于接收端缓冲，不能把 20 fps 当成低延迟保证。

Mac 可以选择打开本地低缓冲预览：

```sh
python3 tools/cast-receiver/server.py --preview
```

它只在收到已认证游戏后启动自己的 `ffplay` 窗口，使用同一 FFmpeg 输出的 loopback UDP MPEG-TS（`127.0.0.1:28768`）；游戏断开自动关闭这个窗口。Kodi HLS 仍可工作。此路径旨在减少 HLS 缓冲，但尚不承诺端到端延迟，也不是电视可直接发现的通用协议。不要同时让预览与 Kodi 出声，否则会回声。

其他电视直接接收 NES、设备独立硬编码、AirPlay 镜像和无损高分辨率 PS1 投屏不在本版实现内。

## 检查

```sh
bash shared/game_cast_tests/run.sh
python3 tools/cast-receiver/tests/server_test.py
bash nes/tests/run.sh
```

客户端测试覆盖延迟接管、三个输出模式、缓冲／暂停、RGB565 字节序、网络阻塞回退和结束清理。服务测试使用真实 FFmpeg 编码并验证 H.264/AAC、4:3、HTTP Range、令牌、拒绝另一发布者、断开回收子进程。测试生成色块／音调，不需要商业 ROM。
