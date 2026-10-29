# 整机屏幕刷新优化

macOS Apple Silicon，QEMU Malta/24Kf、128MB，使用同一段自有 MIPS 连续画面测试程序。正常启动器保持运行，日志管道保持打开；测试程序通过实际 `/dev/fb2` mmap 以 60Hz 连续填充不同颜色，主机按 sequence 变化计数。该素材用于测量采集链路，不代表所有应用都能绘制 60fps。

| 配置 | 主机实际接收 | Chrome 实际绘制 |
| --- | ---: | ---: |
| 原采集等待 100ms | 7.787fps | 未单独测量 |
| 采集等待 16ms | 31.344fps | 31.708fps |

原始可复现命令为 `python3 scripts/frame_refresh_test.py --url http://127.0.0.1:8776 --min-fps 25`：修改前收到 39 帧/5.008 秒，因低于 25fps 失败；修改后收到 157 帧/5.009 秒，通过。Chrome 在连续画面期间实际调用屏幕 canvas 的 `drawImage` 111 次/3.501 秒，测量后恢复原方法。浏览器显示速度与主机接收一致，当前无需另加显示协议。

瓶颈在 `guest/bridge.c` 的 `poll(...,100)`：输入或日志没有变化时，每次采集前都会等待 100ms；之后还要复制、比较和发送 1,088,000 字节 framebuffer。等待缩短为 16ms，保持只发送变化画面和原 framebuffer ABI。应用清单改按单调时钟约五秒刷新，避免加快循环后频繁重发；关闭已经 EOF 的日志描述符，避免应用退出后因 POLLHUP 持续空转。

没有改写应用 ELF，也没有改变 framebuffer 分辨率、键盘与触摸坐标、PNG 格式或浏览器 API。16ms 是单次等待上限，实际采集和发送耗时会降低帧率，不等于保证 60fps。

最终客体通过七个官方 MIPS 应用启动和原始 ELF 哈希回归，见 [smoke-results.json](smoke-results.json)。输入截图确认 [12+3=15](calculator-result.png) 和 [小写字母、数字输入](terminal-input.png) 正常。提高采集频率后的原始 Bilibili 视频测试为 1.006 倍速、9.960fps，暂停和双向跳转通过。日常 8775 实例重新启动后，客体屏幕桥接程序与视频客户端的哈希均与最终构建文件一致。

## 复现

先在构建容器中编译自有测试程序，输出到被 Git 忽略的目录：

```sh
mkdir -p build/debug/display
podman machine start
podman run --rm -v "$PWD:/work" localhost/c1max-builder:bookworm \
  mipsel-linux-gnu-gcc -mabi=32 -mips32r2 -mfp32 -O2 -static \
  scripts/fixtures/frame-probe.c -o build/debug/display/frame-probe
podman machine stop
python3 start.py --port 8776 --disk runtime/refresh-test.qcow2 --audio-backend none
python3 scripts/frame_refresh_test.py --url http://127.0.0.1:8776 --min-fps 25
python3 start.py --stop --port 8776
```

测试程序只临时覆盖测试实例的屏幕内存；测试结束停止其进程并重新打开启动器。请使用独立盘，不在正在操作的日常实例运行。具体统计见 [frame-results.json](frame-results.json)。
