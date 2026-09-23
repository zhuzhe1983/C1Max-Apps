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
└── settings.json         静音、解释器设置
```

不附带商业游戏或 BIOS。没有 BIOS 时使用 HLE，兼容性有限；自备 BIOS 通常更合适。即时存档与镜像完整路径绑定，移动或改名后不会自动找到旧即时存档；记忆卡不受镜像路径影响。

游戏库 W/S 选择、回车开始、R 刷新，V 切换声音，C 切换快速/解释器模式，也可触摸。**默认静音**，本轮遵照用户工作时间要求没有测试发声或音画同步。

CHD 可在电脑用 MAME 的 `chdman verify -i GAME.chd` 校验，再用 `chdman extractcd -i GAME.chd -o GAME.cue -ob GAME.bin` 转为 BIN/CUE。两份文件一起复制进游戏目录，在游戏库选择 `.cue`；原始 CHD 保留在电脑。2026-09-23 用户提供的 imbNES 洛克人合集盘按此转换，真机 HLE＋兼容模式进入了合集菜单和第一款游戏的选关画面。它是在 PS1 上运行 NES 的合集，不能据此推断原生 PS1 3D 游戏性能。

| 操作 | 物理键 |
| --- | --- |
| 方向 | WASD |
| × / ○ / □ / △ | J / K / U / I |
| L1 / R1 | Q / E |
| L2 / R2 | Z / C |
| Start / Select | 回车 / 空格 |
| 暂停菜单 | 中间返回键 |
| 回 launcher | 电源键 |

暂停菜单提供继续、存档、读档、返回游戏库；W/S 选择，回车确认，中间返回直接继续。退出前刷新虚拟记忆卡。画面保持 4:3，左右留黑边；游戏库和模拟器通过 `exec` 交接，同一时间只有一个进程写屏幕。

## 已验证与边界

用本项目自写的 PS-X EXE，在真机分别运行动态重编译和解释器 180 次垂直同步；动态模式峰值约 19.4 MiB，解释器约 5.3 MiB。实际屏幕显示运动三角形，游戏库启动、即时存档、读档、返回菜单已测试。这是自写测试程序的运行内存，不能推断商业游戏的内存、帧率或兼容性。

目前尚未用用户游戏验证 3D/GTE、游戏内存档、实际 BIOS 启动、光盘音轨和多盘兼容性。遇到问题可切换解释器作对比，但速度可能降低。

## 构建与复现

运行仓库 `./tools/build.sh`。CMake 在生成目录中应用两处 GCC 12 的 `abs()` 类型修正，不改上游工作目录；静态 zlib 固定版本及校验值在 `archives.json`。不需要 SDL、X11、OpenGL，也不使用地址 0 映射。

自写测试程序不包含游戏资源：

```sh
docker run --rm -v "$PWD:/work" -w /work c1max-apps-builder:bookworm python3 pcsx4all/tests/build-smoke.py
adb push .build/mips/c1max-psx-core /tmp/
adb push .build/qa/pcsx4all/triangle.exe /tmp/
adb shell 'C1_APPS_DATA=/tmp/psx-test /tmp/c1max-psx-core --image /tmp/triangle.exe --mute --headless --smoke-frames 180'
# 加 --interpreter 测试解释器；图形模式需要先退出其他 framebuffer 应用。
```

PCSX 核心的多数文件为 GPL-2.0-or-later，MIPS 重编译器包含 MIT 许可文件，上游仓库附 GPLv3 文本。设备包保留上游许可文本、作者通知与依赖许可；完整对应源码由固定上游提交、本目录适配代码和构建脚本组成。不要单独分发二进制而遗漏源码获取方式。
