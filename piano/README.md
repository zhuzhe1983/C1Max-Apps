> 已迁移到 `apps/piano`，统一构建/部署见 [apps README](../README.md)。下文保留硬件实现记录，旧 Lima 命令由 `../tools/build.sh` 替代。

# C1 Max 触摸屏钢琴 (piano)

快易典 C1 Max (MP-D350MAX, Ingenic X2000, mipsel, Linux 4.4.94) 上的自制触摸钢琴。
fb2 画钢琴键盘，触摸弹奏，tinyalsa 直接写 PCM 出声。

## 目录
```
piano/
  src/piano.c          主程序（显示 + 触摸 + 合成，单文件）
  ../shared/tinyalsa/  共享 tinyalsa 源码（hw 路径，无插件/无 dlopen）
  build.sh            调用 apps/tools/build.sh 统一交叉编译
  run.sh              调用统一部署工具并打开 apps launcher
  ../.build/mips/c1max-piano   编译产物（静态 MIPS32r2 LSB ELF）
  shots/               截图
```

## 编译
```
./build.sh          # Docker 内统一编译全部 apps
```
产物 `../.build/mips/c1max-piano`：`ELF 32-bit LSB, MIPS32 rel2, statically linked`。
tinyalsa 只编 `pcm.c pcm_hw.c mixer.c mixer_hw.c limits.c`，不定义 `TINYALSA_USES_PLUGINS`
（避免 dlopen，纯静态）。**已修 tinyalsa 一处 bug**：`pcm_close()` 里 `snd_utils_close_dev_node()`
未被 `#ifdef TINYALSA_USES_PLUGINS` 包住，非插件静态构建会链接失败——已补 ifdef。

## 运行
```
ANDROID_SERIAL=MagicPen-931f06 ./run.sh
```
统一部署工具打开 apps launcher，从中选择 Piano。Launcher 负责停止原桌面和退出后恢复。
程序自身支持 `c1max-piano [毫秒]`：参数>0 时跑指定毫秒自动退出（方便截图测试），
0/缺省则一直跑到短按电源键或 SIGTERM。实际打开 framebuffer 前应由统一 launcher 管理桌面状态。

### 白键覆盖黑键修复

白键和黑键的矩形有重叠。增量重绘白键时，必须按以下顺序合成：白键 → 与它相交的黑键。
黑键使用 `g_gate` 的当前状态重绘，因此按住物理黑键再按白键，黑键仍保持蓝色高亮。
`paint_key` 只修改 backbuffer，`draw_key` 在合成完成后一次提交脏区域；
`draw_all` 也只提交一帧，避免先后显示白键覆盖黑键的中间状态。

可运行离屏回归检查，无需停止桌面，也不会打开 framebuffer、触摸或 ALSA：

```sh
adb -s MagicPen-931f06 shell /storage/apps/current/piano/c1max-piano --render-test
```

检查覆盖 24 个键的按下/释放、保持黑键按下再逐个按放白键、增量绘制与完整绘制一致，
以及三个 framebuffer 缓冲的旋转像素一致。此次在宿主机提取实际绘制代码运行 ASAN/UBSAN 验证通过；
禁用相交黑键补画后，同一检查会失败，能捕获本次回归。

## 硬件适配要点（实测）

### 显示 /dev/fb2
- 原生 340×800 32bpp BGRA，stride=1360，3 缓冲（每帧 1088000B）。DPU 旋转 90° → 显示 800×340 横屏。
- 横屏 display(dx,dy)（dx:0..799, dy:0..339）→ 原生偏移 `(799-dx)*1360 + dy*4`。像素 uint32 小端 = 0xAARRGGBB。
- 程序维护一块 800×340 横屏 backbuffer，只在按键高亮变化时把合成后的脏矩形 blit 到 fb2（3 帧都写），省 CPU。

### 触摸 /dev/input/event2 (cst3xx) —— 重要修正
- **此固件的 cst3xx 只上报单点**，不是多点协议 B。`/proc/bus/input/devices` 实测：
  `EV=b`（SYN+KEY+ABS），`ABS=1000003`（仅 ABS_X, ABS_Y, ABS_PRESSURE），`KEY` 只有 BTN_TOUCH。
  **没有** ABS_MT_SLOT / ABS_MT_TRACKING_ID / ABS_MT_POSITION_X/Y。
- 所以按键用单点协议解析：BTN_TOUCH(按下/抬起) + ABS_X + ABS_Y，SYN_REPORT 时判定落点。
- 坐标是原生方向：`tx=ABS_X∈[0,339]`，`ty=ABS_Y∈[0,799]`，映射同显示旋转：`dx=799-ty, dy=tx`。
  （驱动 absinfo 的 min/max 未导出=0，按面板 340×800 取范围；如实测偏移改 piano.c 里 `TX_MAX/TY_MAX` 校准。）
- 合成器仍做成**复音**（每键独立振荡器+AR 包络，释放中的音继续响），所以单点快速滑奏/断奏听感自然；
  触屏无法多指同时按下多键；物理键盘可以同时保持多个音符。

### 音频 ALSA card0 "mypai" (ES8326 codec + AW87xxx PA)
出声**必须先打开功放/喇叭通路**（默认是关的），关键 mixer 控件（`amixer -c0 controls`）：
| numid | name | 默认 | 出声需设 |
| --- | --- | --- | --- |
| 1 | `aw87xxx_profile_switch_0` | Off(2) | **Music(0)** 使能 AW87xxx 功放 |
| 27 | `SPKPA_L` | ZERO(0) | **Switch(1)** DAC→左喇叭 |
| 28 | `SPKPA_R` | ZERO(0) | **Switch(1)** DAC→右喇叭 |
| 29 | `softvolume` | 依系统当前值 | 保留原音量，不强制调到 255 |
| 6 | `DAC Playback Volume` | 188/190 | 已够大 |

当前 `run.sh` 调用统一部署工具；piano 内部用 tinyalsa mixer 启用功放和喇叭通路（`enable_speaker()`），保留系统音量。
PCM 参数（aplay -v 实测硬件接受）：card0 dev0，S16_LE，44100Hz，stereo，优先尝试 period=1280×8buf，再尝试候选列表。

## 合成器
- 24 键 = 2 个八度 C4..B5（14 白 + 10 黑）。每键一个正弦振荡器（1024 点查表 + 相位累加）+ AR 包络
  （起音 ~4ms，释放 ~180ms）。单线程 `pcm_writei` 定拍：每轮非阻塞排空触摸事件→更新门→渲染一个 period→写 PCM（阻塞即定时）。
- CPU 很省：只有 gate 开或包络未归零的键才参与混音。

## 验证状态（2026-09-21 设备实测）
- **出声（tinyalsa）✅**：`aplay -v` 先验证链路（44100/S16/stereo 完整 setup 无错）。tinyalsa 版实测
  `pcm_open` 用候选 `period=1280 × 8buf`（cand0，Ingenic AS 驱动只吃这个几何；256/512/1024 等均被拒
  `cannot set hw params: Invalid argument`，故程序内置候选列表逐个尝试）。连续 `pcm_writei`：
  3s 跑写 **131840 帧**、5s 跑写 **220160 帧**，`write_err=1`（仅开机第一次 underrun，pcm_prepare 恢复）。
  即 PCM 写入全程正常。判据为写入无错 + mixer 开关正确（无法远程听声）。
- **画钢琴 ✅**：见 `shots/device_keyboard.png`（fb2 实拍）——2 个八度键盘正确显示，黑键分组(2/3)正确。
- **多点触摸 ⚠️单点**：`shots/device_keyboard.png` 中一个白键(G4)被按下高亮=触摸事件经
  `hit_key→draw_key(pressed)→blit` 链路实拍验证。**但此固件 cst3xx 仅单点**（见上），无法硬件多指同时按；
  合成器已做成复音（释放中的音继续响），单点滑奏/断奏听感自然。
- **共享设备注意**：本机 audio(pcmC0D0p) 与 fb2 均为单占用，三任务共享时会与兄弟任务（实测有个
  `/storage/c1max_nes` NES 模拟器）抢占，报 `Device or resource busy`。**切勿 `kill -9` piano**——
  -9 不会走 `pcm_close`，会把 ALSA substream 卡在 SETUP 占着不放，阻塞后续所有音频；用 SIGTERM/正常退出。

## 已知可改进
- 触摸坐标范围按面板 340×800 硬编码（driver absinfo min/max 未导出）；如实测有偏移，调 `TX_MAX/TY_MAX`。
- 缓冲 1280×8=10240 帧 ≈232ms，弹奏延迟偏高（受驱动几何限制）。可试 `start_threshold`/更小 period_count 压低。
- `--render-test` 覆盖纯绘制逻辑；触摸坐标和硬件按键仍需实机交互验证。
