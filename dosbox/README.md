# DOSBox / C1Max

基于 [DOSBox Pure](https://github.com/schellingb/dosbox-pure) 的原生 MIPS Linux 移植，固定提交见 `dependencies.json`。轻量 libretro 前端直接使用 framebuffer、实体键盘、触摸鼠标和 tinyalsa，不安装 RetroArch、SDL 或桌面环境。

## 使用

launcher 第二页打开 **DOSBox**。首开自动复制本项目原创的 `C1LAB/C1LAB.COM` 测试程序；已有文件不会被覆盖。它没有声音，用方向键或触摸收集黄色方块，中间返回键退出时保存 `RESULT.DAT`。只附带这个原创小程序，不附商业 DOS 游戏或系统镜像。

0.1.1 修复内置 DOS LAB 每轮清空 VGA 显存导致的闪烁，改为只重画变化区域；前端对相同像素、相同画幅和提示条的画面不再重复翻页。已有 0.1.0 用户若保留了旧示例，需要备份后用 release 中的新版 `C1LAB.COM` 替换；游戏存档 `RESULT.DAT` 可保留。

将游戏解压后的**整个目录**复制到：

```text
/storage/apps/data/dosbox/
├── games/
│   ├── C1LAB/{C1LAB.COM,RESULT.DAT}
│   └── MYGAME/{GAME.EXE,...其他游戏资源}
├── settings.json
└── session.conf
```

游戏库扫描三层子目录、最多 200 项 `.EXE` / `.COM` / `.BAT`。启动文件使用英文数字、下划线或短横线的 DOS **8.3** 文件名，例如 `GAME.EXE`；不会自动修改用户文件名。游戏目录挂为 DOS 的 `C:`，普通游戏存档直接保存在该目录。启动 BAT 时使用 `CALL`，程序结束回到游戏库。首版不提供 ZIP/CD 镜像选择、Windows 安装或即时存档。

W/S 选择、Enter 开始、R 刷新；D 进入 DOS 命令行，G 切换游戏／文本键盘，H 查看按键与设置。命令行的 `C:` 是整个 `games/` 目录；支持 `DIR`、`CD`、`TYPE`、重定向等 DOS 内置命令，`EXIT` 回游戏库。它不是设备 Linux shell；需要 Bash 时使用“终端”应用。

默认 8 MB DOS 内存、2750 固定 cycles、静音。设置可选 16 MB、4720/7800 cycles、16:9和声音。提高 cycles 会增加主机负担，不保证更快。声音接口保留，但遵照用户工作时间要求尚未测试发声和音画同步。

## 实体键盘

- 电源：回 launcher；长按拍照键至少 650 ms 后松开：暂停菜单。
- 右上小方键：Backspace；中间长键：DOS Esc；右下小方键：Enter。
- 文本模式：Shift＋字母输入键帽数字／符号；快速双击 Shift 切换大写，屏幕显示短暂模式提示。
- 游戏模式：WASD 方向；J Ctrl、K Alt、U 空格、I Enter；Shift 作为真正的 DOS Shift，其余字母按原字母传递。游戏需要输入名字／命令时，在暂停菜单切到文本模式。
- 触摸屏：直接定位鼠标并按左键；游戏本身需要支持 DOS 鼠标。4:3 黑边不触发点击。
- 拍照键短按循环一次性前缀，下一键使用后自动恢复；中间返回取消前缀：

| 前缀 | 下一键 |
| --- | --- |
| NAV（1 次） | WASD 方向、Q/E Home/End、Z/X PgUp/PgDn、B Delete、空格 Tab |
| FN（2 次） | QWERTYUIOPAS 对应 F1–F12 |
| Ctrl（3 次） | 下一字母组成 Ctrl 组合，按住字母即保持组合 |
| Alt（4 次） | 下一字母组成 Alt 组合 |
| 符号（5 次） | Q[ W] E{ R} T< Y> U= I+ O_ P\ A" S' D` F! G\| H^ |

暂停菜单可继续、切换输入方式／画幅和返回游戏库。没有屏幕虚拟键盘。默认保持 4:3；DOS 命令行自动16:9以显示 80 列。显示最多约 30 次/秒，模拟核心按其刷新率推进；这不等于游戏性能保证。

## 适配与边界

目前只发布 **normal 解释器**。上游 MIPSel 默认使用未对齐指针读取，与 X2000 不兼容；`tools/prepare.py` 改成安全的 guest 内存访问。动态重编译在真机仍崩溃，因此编译时关闭，不向用户暴露未验证选项。保留完整 TLB；未采用上游注释里已失效的 partial TLB 方案。

为节省内存，移除 MT-32、SC-55 和 SoundFont 合成器，MIDI 默认关闭，Voodoo 关闭。完整图形前端与测试程序在真机峰值约 32 MiB。这个数字只代表原创 DOS LAB；早期 2D DOS 程序是首版目标，复杂 3D 游戏、保护模式游戏和 Windows 软件没有通过兼容性验证。

游戏库与核心通过 `exec` 交接，同一时刻只有一个应用写 framebuffer。必须由 launcher 监督器启动以释放原厂桌面内存；不能在原厂界面占用大量内存时直接另开模拟器。按电源结束仿真不会替游戏执行“保存”，应先用游戏自身存档功能。

## 构建与验证

仓库根目录运行 `./tools/build.sh`。构建器安装 NASM，直接从 `tests/lab.asm` 生成原创 COM；其余为静态 MIPS ELF。

```sh
sh dosbox/tests/run.sh
# 无图形的限定帧数核心检查（仍需设备有足够空闲内存）：
C1_APPS_DATA=/tmp/dos-qa ./c1max-dos-core --file /path/C1LAB.COM --headless --mute --frames 180
```

键盘的 Shift 数字/大写、真实退格/Esc/Enter、方向/F键/Ctrl/Alt/符号前缀、按住/释放和丢事件恢复有 ASan/UBSan 状态机测试。真机证据与未验证范围见根目录 `VALIDATION.md`。

核心与适配层 GPL-2.0-or-later；原创 DOS LAB 为 MIT。许可证、上游作者与第三方通知位于 `licenses/`，共享 LVGL/tinyalsa/font 通知随应用包保留。对应源码是本仓库本目录、共享代码、构建脚本及固定提交的 DOSBox Pure；对外分发时一起提供这些源码与构建说明。


## ZIP / DOSZ 游戏包（0.1.2）

游戏库可以直接打开完整 `.zip` / `.dosz`，包名支持中文，无需在设备解压；包内原始目录和文件保留。DOSBox Pure 在包内提供启动文件列表，使用**游戏键盘模式 W/S＋回车**选择 `.EXE` / `.COM` / `.BAT`。包里可能有安装或声音设置工具，应选择游戏启动程序；某些包只有一个入口会自动启动。

原始压缩包只读，游戏写入通过 DOSBox Pure 的独立 `<包名>.pure.zip` 保存到 `/storage/apps/data/dosbox/`，退出前正常刷新；不要重命名包或在不同目录使用同名包，否则会改变或共用对应存档。散装 DOS 程序仍直接读写其所在目录。此功能没有加入虚拟键盘，按键说明沿用本应用的实体键适配。

ZIP 内的 `dosbox.conf` 不自动覆盖当前内存、CPU 与声音设置。默认仍为 8 MB、2750 cycles、解释器和静音；需要的 DOS 内存可在 H 设置页切到 16 MB。文件可导入不等于所有游戏都能流畅运行：Windows 程序、部分光盘安装、复杂保护模式游戏和高负载游戏需要分别验证。


### 两侧实体键提示

默认 **4:3（453×340）**，可在长按拍照打开的菜单中切换到 **16:9（604×340）**。即使在 16:9，两侧各保留 98 像素给提示，游戏不会拉满 800 像素或覆盖提示。DOS shell 同样遵循此设置；触摸鼠标只在中间游戏区域有效。

游戏模式显示 WASD 方向与 J Ctrl、K Alt、U 空格、I 回车；文本模式显示 Shift 数字、常用符号和当前大小写状态。短按拍照后，左侧改为当前一次性前缀的具体映射（导航、F1–F12、Ctrl、Alt、额外符号），消费前缀或按返回取消后恢复正常。右侧始终显示确认、Esc、退格、触摸、拍照和电源。没有游戏内临时底部遮挡条。
