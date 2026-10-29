# 同步最新上游与模拟器整合验证

本次在 macOS Apple Silicon 上，从仓库内实际的 `simulator/` 目录启动 QEMU Malta/24Kf、128MB、Linux 6.6.60，并使用独立盘 `runtime/pr-update-test.qcow2`。原有个人数据盘未参与测试。

## 更新内容

- 基础源码从 `69179e11c105193bc3888d142169ef4f42a24d31` 更新到上游当日 main 的 `1b7ac9f15f953a9459d05ae8b2efd30fb2fd5a3b`。
- 官方二进制目录使用该提交的 `store/catalog.json`，release 为 `nes-0.2.1`；NES 从 0.2.0 更新至 0.2.1。源码清单中的 Bilibili 0.3.0 等版本尚无对应官方发布包，仍使用可验证下载的 Bilibili 0.2.0 等原始包。
- 最新共享音频服务增加 `cast_client.cpp`、`output_mode.cpp` 依赖；交叉编译补齐链接，隔离复制 nlohmann/json 头文件并保留其 MIT 声明，没有加入主机 libc 头目录，也没有改动上游应用源码。
- 新增仓库根 README 入口；首次运行需构建镜像，运行参数和调试说明位于模拟器 README。模拟器缓存、镜像、录音、数据盘和账号均不提交。

`python3 scripts/build.py --engine podman` 完整入口通过，校验下载源码和官方包，生成内核、驱动及十个 MIPS 程序。复用已有 `c1max-build` volume 与构建镜像，不宣称空缓存全量构建；更新后的基础源码和本机下载目录重新准备。FFmpeg 官网出现临时 TLS 错误后重试成功，最终归档通过固定 SHA-256 校验。

## 实际回归

| 检查 | 结果与证据 |
| --- | --- |
| 七个官方应用启动、原始 ELF 哈希 | 通过，[smoke-results.json](smoke-results.json) |
| 数字与小写字母输入 | 计算器 [12+3=15](calculator.png)，终端 [abc 123](terminal.png) |
| Bilibili 原始 MIPS 视频 | 640×360、10fps 自有 H.264/AAC：1.009 倍速、9.990fps；播放画面变化、暂停稳定、暂停中前跳、恢复及后退通过，[统计](video-results.json)、[画面](video.png) |
| 四种播放请求头参数组合 | 均返回 200、客体退出 0，[headers-results.json](headers-results.json) |
| 真实 MIPS framebuffer 连续画面 | 174 帧/5.003 秒，34.781fps，[frame-results.json](frame-results.json)；没有重新测量浏览器绘制帧率 |
| 最新共享音频 IPC 服务 | 播放位置达到 577ms，暂停/恢复及正常退出通过，[audio-service-results.json](audio-service-results.json) |
| 实际 AC97 录音 | 48kHz、16-bit 立体声，1,264,094 帧、2,094,297 个非零样本，检测到音频服务素材的 660Hz 与视频素材的 880Hz，[audio-results.json](audio-results.json) |
| NES 0.2.1 网页安装与原始浏览器启动 | 原始 ELF SHA-256 核对一致，[nes-results.json](nes-results.json)、[画面](nes.png)；未导入或运行 ROM |
| 已加载构建产物 | 客体内驱动、屏幕桥接、视频客户端、启动器、音频及音量服务六个文件的哈希与最终构建一致，[loaded-artifacts.json](loaded-artifacts.json) |
| 正常关闭与数据盘检查 | 模拟器正常关闭，`qemu-img check` 无错误 |

主机服务 `python3 scripts/video_service_test.py -v` 三项通过：实际 FFmpeg/ffprobe 的 Range/请求头与自然结束、暂停精确跳转/恢复、停止后的子进程与临时文件清理。

## 音频服务复现

先完成镜像构建，再在同一构建镜像中生成小型 MIPS IPC 测试客户端：

```sh
mkdir -p build/debug/audio-service
podman machine start
podman run --rm -v "$PWD:/work" localhost/c1max-builder:bookworm \
  mipsel-linux-gnu-gcc -mabi=32 -mips32r2 -mfp32 -O2 -static \
  -I.cache/upstream/C1Max-Apps/shared scripts/fixtures/audio-service-probe.c \
  build/objects/audio_client.o -o build/debug/audio-service/audio-service-probe
podman machine stop
python3 scripts/audio_service_test.py --url http://127.0.0.1:8776
```

需先另开终端启动独立盘实例。客户端明确使用 `owner=NONE` 启动共享服务，再发送播放请求；新版上游客户端的应用 owner 默认查找应用自身随包服务，不能用尚未发布新版的钢琴包替代此项测试。测试 HTTP 素材和音频数据目录均为临时自有数据，不依赖外部账号。

当前仍是应用兼容环境。Windows/Linux 主机、真机固件行为、投屏接收器及游戏 ROM 运行不在本次验收范围。
