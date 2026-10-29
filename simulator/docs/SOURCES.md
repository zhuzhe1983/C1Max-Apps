# 项目来源与架构

基础源码和官方包检查日期：2026-10-29；c1max-suite 对照保留 2026-10-07 的记录。

| 项目 | 本次检查提交 | 作用 |
|---|---|---|
| [C1Max-Apps](https://github.com/zhuzhe1983/C1Max-Apps) | `1b7ac9f15f953a9459d05ae8b2efd30fb2fd5a3b` | Ingenic X2000/MIPS 的设备应用；本项目使用官方发布包与基础运行时源码 |
| [c1max-suite](https://github.com/typixdeck/c1max-suite) | `a213284771b5f43da1292f20bddbd4789a06beb4` | Raspberry Pi OS ARM64 桌面移植；GTK3/Cairo 显示输入与原生桌面音视频接口 |

第二个项目的 `docs/SOURCE-PROVENANCE.json` 记录其导入的 C1Max-Apps 提交为 `b7930c572715688595bb3bba1fbdc7a08df9e88a`；它导入的是另一个历史快照。它的 ARM64 `.deb` 不适用于当前 MIPS 客体。

模拟器的 `catalog.json` 固定上游 `store/catalog.json` 中实际发布的包目录（release `nes-0.2.1`），包括 NES 0.2.1。仓库根目录 `catalog.json` 是源码版本清单；其中一些更新尚未发布为官方应用包，因此不会据此虚报模拟器中可下载的版本。基础启动器、音频和音量服务由上述最新源码编译；音频服务补齐投屏客户端和输出模式的链接依赖，默认仍为本机输出。投屏接收器、完整桌面镜像尚未在模拟器中验收。

模拟器的数据流是：原始设备 ELF → MIPS Linux 系统调用 → framebuffer/evdev/ALSA 适配 → QEMU；浏览器接收 BGRA framebuffer 并旋转显示，输入按原设备坐标转换。设备 framebuffer 为 340×800、32 位 BGRA、三页、stride 1360；浏览器横屏坐标 `(x,y)` 对应物理坐标 `(y,799-x)`。

应用的 HTTPS 请求经过客体 `wget` 协议适配和本机 HTTP 桥接。主机实际执行 HTTPS 并校验证书，保留应用要求的方法、请求体和请求头；这解决设备 GNU wget 参数与 BusyBox 的差异，也允许按次使用主机代理。纯音频解码由客体 MIPS FFmpeg 完成。2026-10-08 起视频默认使用主机安装的 FFmpeg/ffprobe 解码与缩放，YUV420 和伴音 PCM 经会话协议送回 MIPS 客体，原始应用负责显示，客体 OSS/AC97 负责声音输出；`--video-backend guest` 保留客体软件解码路径。默认监听地址和调试端口均为回环地址。

应用包来源 URL、包 SHA-256、原始 ELF SHA-256 和架构由 `scripts/fetch_apps.py` 保存。基础程序由原源码生成，其提交和 ELF SHA-256 单独记录为 `upstream-source-build`，不会冒充官方下载二进制。
