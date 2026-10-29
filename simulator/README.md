# C1 Max QEMU 模拟器

在电脑上运行 C1 Max 官方应用包中的 **原始 MIPS 二进制**。浏览器显示设备 framebuffer，并把键盘和触摸送入客体 Linux；应用逻辑没有改写成网页。

上游：[zhuzhe1983/C1Max-Apps](https://github.com/zhuzhe1983/C1Max-Apps)，基础源码固定在 `upstream.json` 的提交。应用包固定在 `catalog.json`，下载后校验包和每个文件的 SHA-256。上游没有提供可下载的启动器基础 ELF，所以启动器、音量和音频服务由上游源码交叉编译成 MIPS；七个预装应用直接使用官方包，详见浏览器中的“二进制来源与哈希”。

## 启动和停止

主机需要 Python 3.11+、QEMU、FFmpeg，以及首次构建使用的 Docker 或 Podman。首次使用先在仓库根目录进入 `simulator/` 并构建：

单独保存模拟器目录时，在 `start.py` 所在目录执行相同命令，省略 `cd simulator`。

```sh
cd simulator
python3 scripts/build.py --engine podman
python3 start.py
```

打开 <http://127.0.0.1:8775>。停止可在运行终端按 Ctrl+C，或另开终端执行：

```sh
# 在 simulator/ 目录执行
python3 start.py --stop
```

默认数据盘是 `runtime/data.qcow2`，正常关闭会停止应用、同步并卸载磁盘。重新启动保留应用安装与个人数据。C1 Slim 使用 8765，本项目使用 8775，目录、磁盘和构建缓存独立。

macOS 安装依赖：`brew install qemu python ffmpeg podman`。运行已构建镜像不需要 Podman；首次构建需要 Docker 或 Podman、Git、curl 和联网。Linux/Windows 也需要让 `ffmpeg`、`ffprobe` 在 PATH 中；Windows 构建建议在 WSL2/Docker 内进行。

```sh
# macOS 初次准备构建虚拟机
podman machine init --cpus 4 --memory 8192
# 在 simulator/ 目录执行
python3 scripts/build.py --engine podman
# 后续只修改设备适配时
python3 scripts/build.py --engine podman --runtime-only
```

Docker 用户先启动 Docker，再用 `python3 scripts/build.py --engine docker`。构建使用独立的 `c1max-build` volume；依赖下载缓存在 `.cache/`，生成物在 `build/`。不依赖 C1 Slim 目录。海外 GitHub 下载遇到连接问题时可仅给该命令设置代理；已有本地代理示例：

```sh
https_proxy=http://127.0.0.1:6789 http_proxy=http://127.0.0.1:6789 all_proxy=socks5://127.0.0.1:6789 python3 scripts/build.py
python3 start.py --proxy http://127.0.0.1:6789
```

## 屏幕、键盘、声音和应用

- 默认横屏 800×340；浏览器点击、拖动模拟触摸。竖屏应用可按“旋转屏幕”。
- 屏幕采集每轮最长等待 16ms；连续画面的本机测试约 31fps，Chrome 实际显示约 32fps。应用自身绘制和内容复杂度仍影响刷新率，测量见 [屏幕刷新验证](docs/verification/2026-10-08/frame-refresh.md)。
- 电脑数字键 1–0 直接送入数字键码；字母键正常输入小写。设备按键规则是双击 Shift 切换大小写，按住 Shift＋Q–P 输入数字；其余符号见网页键帽。符号键、方向键、退格、Enter、音量键可用。Mac 的 Command 快捷键交给浏览器。
- 默认使用 macOS CoreAudio；Linux/Windows 从 QEMU 可用音频后端选择。无音频环境可用 `--audio-backend none`。
- 预装钢琴、日历、计算器、终端、五子棋、CrossPoint 和应用商店。网页侧栏选择未安装应用会按需下载、校验并安装；原始应用商店也可刷新目录并独立安装。两者共用商店目录和菜单，安装记录保存在数据盘。
- 纯音频的 MP3、FLAC、AAC 解码在 MIPS 客体内完成，经 AC97 输出。`mplayer` 转发原应用的 `User-Agent`、`Referer`，支持音频播放、暂停和位置查询；纯音频路径暂不支持跳转。
- Bilibili 默认由本机 FFmpeg 解码、缩放 H.264 视频，并将 YUV420 帧送回客体的原始 FIFO；原始 MIPS 应用继续绘制控件和 framebuffer。视频伴音也由本机解码为 PCM，再经客体音量控制和 AC97 输出。支持暂停、位置查询、暂停中预览及双向跳转；画面输出上限 20fps，接收端丢弃积压帧以保持正常播放速度。
- `--video-backend guest` 可切回完全在 MIPS 客体内解码的路径，也可在没有本机 FFmpeg 时使用；QEMU 中这条路径较慢。两种模式均未模拟 X2000 VPU，也不修改官方应用 ELF。

通过网页侧栏切换应用会停止上一应用和其后台工作进程；在原生桌面内部切换按上游自己的后台规则执行。

录制虚拟声卡输出：`python3 start.py --audio-record build/test.wav`。录音文件必须不存在，正常关闭后写完 WAV 头。

Bilibili 403 修复见 [原验证记录](docs/verification/2026-10-07/bilibili.md)，视频加速的测量和控制回归见 [加速验证](docs/verification/2026-10-08/video-acceleration.md)。复现测试建议使用独立盘：

```sh
python3 start.py --port 8776 --disk runtime/video-test.qcow2 --audio-record build/video-test.wav
# 另一个终端，需要电脑上的 ffmpeg 来生成自有测试视频
python3 scripts/mplayer_headers_test.py --url http://127.0.0.1:8776
python3 scripts/video_service_test.py -v
python3 scripts/bilibili_video_test.py --url http://127.0.0.1:8776 --min-speed 0.8 --min-fps 8
python3 start.py --stop --port 8776
```

同步最新上游、NES 0.2.1 安装、共享音频服务和实际 MIPS 回归见 [2026-10-29 验证](docs/verification/2026-10-29/README.md)。基础源码固定 `1b7ac9f`，应用版本以可下载的官方发布包为准。

## 调试与边界

```sh
# 在真实 MIPS Linux 串口中执行命令
python3 scripts/console.py 'uname -a; ls /storage/apps/current; df -h /storage'
# 查看串口启动日志
python3 scripts/console.py
# 打开客体 GDB 端口；使用支持 MIPS 的 GDB 连接 127.0.0.1:1234
python3 start.py --gdb 1234
# 临时独立盘，不影响日常数据
python3 start.py --port 8776 --disk runtime/test.qcow2
python3 start.py --stop --port 8776
```

自开发的静态 MIPS32r2 小端 O32/FP32 应用，可通过串口用 `/usr/bin/wget` 从电脑 HTTP 服务下载至设备目录，再在客体 shell 中启动。运行程序可以访问 `/dev/fb2`、`/dev/input/event0`、`event1`、`event2`、ALSA 和共享字体。网页侧栏目前列出固定上游目录，串口可直接运行自己的程序。运行时、应用日志在 `runtime/boot.log`（其他端口为 `runtime/boot-端口.log`）与网页“程序输出”中；账号数据和镜像不会进入 Git。

这是一套应用兼容环境：QEMU 使用 Malta/24Kf 和标准上游 Linux **6.6.60**，不是自行写了一套 Linux，也不是设备的 X2000/Linux 4.4 固件。自写的 `c1maxsim` 模块补齐 framebuffer、evdev 触摸/键盘、背光、电池和音量接口。真实设备内核依赖 Ingenic 外设、启动信息和驱动，不能直接换入 Malta 就得到整机仿真。默认 128 MB RAM；性能与真实设备不同。

摄像头、USB HID、真实 Wi-Fi 管理、硬件视频解码及厂商电源/休眠流程尚未模拟。联网账户和外部服务需要用户自行配置，应用能启动不代表这些功能已经验收。macOS 已执行实际验证；Windows/Linux 的启动代码具备跨平台处理，但尚未在相应主机上运行验收。

另一个 [typixdeck/c1max-suite](https://github.com/typixdeck/c1max-suite) 把应用移植到树莓派 ARM64/GTK3 桌面，适合原生 Linux 桌面使用。本项目保持原始 MIPS 包运行路线，没有把其 ARM64 包混入客体。比较见 [项目来源与架构](docs/SOURCES.md)。

许可证：本模拟器自己的用户态代码遵循 GPL-3.0-or-later，驱动遵循 GPL-2.0-only。Linux、BusyBox、FFmpeg、OpenSSL、字体与应用各自遵循原许可证。C1Max-Apps 未声明统一仓库许可证；保留每个包和基础源码中已有的第三方声明，不把上游代码统一改标 GPL。依赖声明位于构建目录及客体资源中；本仓库不提交下载的应用二进制、字体、数据盘或账户配置。
