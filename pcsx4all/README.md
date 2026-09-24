# PCSX4all / C1Max

PlayStation 1 原生模拟器移植。基于 [dmitrysmagin/pcsx4all](https://github.com/dmitrysmagin/pcsx4all)，提交固定在 `dependencies.json`。保留 MIPS→MIPS 动态重编译、解释器备选、Unai 软件 GPU 和 PCSX-ReARMed SPU，适配 C1Max 的 800×340 旋转 framebuffer、物理键盘和 tinyalsa。

## 使用

在 launcher 第二页打开 PCSX4all。游戏库支持 BIN/CUE、IMG、ISO、PBP 以及 PS-X EXE，最多扫描 200 项、三层子目录。多轨光盘选择 `.cue`，镜像文件与音轨保持相对路径。不支持 CHD，首版没有运行中的换盘界面。

将自己的文件放在：

```text
/storage/apps/data/pcsx4all/
├── roms/                 游戏镜像（用户提供）
├── bios/scph1001.bin      可选，恰好 512 KiB
├── memcards/slot1.mcr     两张共享虚拟记忆卡
├── memcards/slot2.mcr
├── states/               每个镜像路径一个即时存档槽
├── settings.json         静音、解释器设置
└── display.conf          画幅设置（0 原比例，1 全宽）
```

不附带商业游戏或 BIOS。没有 BIOS 时使用 HLE，兼容性有限；自备 BIOS 通常更合适。即时存档与镜像完整路径绑定，移动或改名后不会自动找到旧即时存档；记忆卡不受镜像路径影响。

游戏库 W/S 选择、回车开始、R 刷新，V 切换声音，C 切换快速/解释器模式，也可触摸。**默认静音**；开启声音时核心会先启用设备扬声器功放和左右声道，再打开 tinyalsa 输出。PCM 由单独线程写入一个有界队列，避免声卡阻塞模拟器画面；设备来不及播放时丢弃积压的旧音频块以维持实时性。Launcher 退出时恢复进入前的混音器状态。

核心每秒把实测 VBlank 帧率与进程 CPU 占用写到 `/storage/apps/data/launcher/hotkey-launch.log`。GPU 主动关闭画面时也会记录黑屏起止和持续毫秒数；若 FF7 黑屏时没有这类事件，更可能是游戏在读盘或生成战斗场景。限速器只限制高于 PAL 50 / NTSC 60 FPS 的运行，不会把低帧率压低；动态重编译和自动跳帧已默认启用。设备没有 3D GPU，FF7 的 3D 场景仍可能受 MIPS 单线程模拟和 Unai 软件渲染限制。切换 4:3 可减少输出缩放像素数；关闭限速不会让核心更快，反而可能造成模拟速度失真。

CHD 可在电脑用 MAME 的 `chdman verify -i GAME.chd` 校验，再用 `chdman extractcd -i GAME.chd -o GAME.cue -ob GAME.bin` 转为 BIN/CUE。两份文件一起复制进游戏目录，在游戏库选择 `.cue`；原始 CHD 保留在电脑。2026-09-23 用户提供的 imbNES 洛克人合集盘按此转换，真机 HLE＋兼容模式进入了合集菜单和第一款游戏的选关画面。它是在 PS1 上运行 NES 的合集，不能据此推断原生 PS1 3D 游戏性能。

| 操作 | 物理键 |
| --- | --- |
| 方向 | WASD |
| × / ○ / □ / △ | J / K / U / I |
| L1 / R1 | Q / E |
| L2 / R2 | Z / C |
| Start / Select | 回车 / 空格 |
| 原比例 / 全宽切换 | 拍照键 |
| 暂停菜单 | 中间返回键 |
| 提示退出 | 短按电源键；屏幕提示“长按五秒电源键退出” |
| 退出游戏 | 按住电源键五秒 |

暂停菜单提供继续、显示模式、存档、读档、返回游戏库；W/S 选择，回车确认，中间返回直接继续。退出前刷新虚拟记忆卡。游戏库和模拟器通过 `exec` 交接，同一时间只有一个进程写屏幕。

### 画面与文字

0.1.1 从 PS1 原始 VRAM 直接缩放到屏幕，移除旧端口先压成 320×240 的中间步骤；同时关闭高分辨率 GPU 跳列／跳行。支持 15-bit 游戏画面和 24-bit FMV、VRAM 环回、有效画面垂直居中。PS1 不同场景可以动态改变分辨率，游戏内字体仍由游戏本身决定。

- **原比例**：约 453×340 的 4:3 画面，两侧静态提示实体方向、动作、肩键、Start/Select、返回及电源。提示绘制在空白区域，不覆盖游戏。
- **全宽**：800×340，横向放大文字，不裁掉上下的对话框；人物也会横向变宽。拍照键随时切换，设置保留到下次启动。全宽没有侧栏，暂停菜单仍显示完整按键提示。

两种模式都使用直接像素采样；全宽能避免 512／640 宽画面先缩至 320 导致的横向笔画丢失，但不能补出游戏原本没有的细节。480／512 行画面仍受屏幕只有 340 行的限制。保留高分辨率绘制会增加该类场景的 GPU 开销，不能保证所有游戏帧率不变。


## 已验证与边界

用本项目自写的 PS-X EXE，在真机分别运行动态重编译和解释器 180 次垂直同步；动态模式峰值约 19.4 MiB，解释器约 5.3 MiB。实际屏幕显示运动三角形，游戏库启动、即时存档、读档、返回菜单已测试。这是自写测试程序的运行内存，不能推断商业游戏的内存、帧率或兼容性。

目前尚未用用户游戏验证 3D/GTE、游戏内存档、实际 BIOS 启动、光盘音轨和多盘兼容性。遇到问题可切换解释器作对比，但速度可能降低。

## 构建与复现

运行仓库 `./tools/build.sh`。CMake 在生成目录中应用 GCC 12 的 `abs()` 类型修正及原生输出所需的 GPU 行绘制修正，不改上游工作目录；静态 zlib 固定版本及校验值在 `archives.json`。不需要 SDL、X11、OpenGL，也不使用地址 0 映射。

自写测试程序不包含游戏资源：

```sh
docker run --rm -v "$PWD:/work" -w /work c1max-apps-builder:bookworm python3 pcsx4all/tests/build-smoke.py
adb push .build/mips/c1max-psx-core /tmp/
adb push .build/qa/pcsx4all/triangle.exe /tmp/
adb shell 'C1_APPS_DATA=/tmp/psx-test /tmp/c1max-psx-core --image /tmp/triangle.exe --mute --headless --smoke-frames 180'
# 加 --interpreter 测试解释器；图形模式需要先退出其他 framebuffer 应用。
```

PCSX 核心的多数文件为 GPL-2.0-or-later，MIPS 重编译器包含 MIT 许可文件，上游仓库附 GPLv3 文本。设备包保留上游许可文本、作者通知与依赖许可；完整对应源码由固定上游提交、本目录适配代码和构建脚本组成。不要单独分发二进制而遗漏源码获取方式。
