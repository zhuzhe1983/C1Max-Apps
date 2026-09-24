# C1Max Terminal

原生 MIPS Linux 终端：LVGL 9.5 绘制 800×340 横屏，PTY 中启动交互式
Shell，使用 libvterm 0.3.3 维护真实终端状态。不是 Android 应用，也不依赖
FbTerm、X11、Wayland 或屏幕虚拟键盘。

## 使用

- 默认优先 `linux-tools/bin/bash`，其次 `tools/bin/bash`、`/bin/bash`、`/bin/sh`。
  可用 `C1_TERMINAL_SHELL` 指定一个可执行文件的绝对路径；不解析 Shell 命令字符串。
- 普通字母直接输入；Shift 组合遵循真实键帽。共享驱动用双击 Shift 切换大写，
  底部 `abc` / `CAPS` 显示当前状态。
- 右上退格为终端 DEL 字节；确认发送回车。中间返回发送 Escape；在前缀模式中
  返回只取消前缀。电源键退出终端回 launcher，不是让词典关机。
- 输入 `exit` 结束 Shell 后保留最后输出，按电源键回 launcher。
- 没有屏幕退出按钮、屏幕键盘或可意外点击的隐藏控件。

“符号”键是一次性前缀，后续操作结束自动恢复普通输入。再按一次可轮换模式：

| 按法 | 下一个键 | 作用 |
| --- | --- | --- |
| 符号一次 | A–Z | Ctrl-A…Ctrl-Z，含 Ctrl-C 中断、Ctrl-D EOF、Ctrl-L 清屏、Ctrl-Z 挂起 |
| 符号一次 | 空格 | Tab 补全 |
| 符号两次 | W / A / S / D | 上 / 左 / 下 / 右 |
| 符号两次 | Q / E | Home / End |
| 符号两次 | Z / X | Page Up / Page Down，发送给前台应用 |
| 符号两次 | B / 空格 | Delete / Tab |
| 符号两次 | R / F / T | 本地历史上翻 / 下翻 / 回到实时输出 |
| 符号三次 | Q W E R T Y U I O P | `= + _ \| \ " ' < > !` |
| 符号三次 | A S D F G H | `[ ] { }`、反引号、`^` |

例如 `ls | less` 的管道是“符号、符号、符号、R”。Ctrl-S 会停止终端输出，
用“符号、Q”（Ctrl-Q）恢复，这是正常 PTY 软件流控。

## 当前设备的命令行环境

2026-09-23 实机核对：Buildroot 2020.02.1、MIPS 小端架构、BusyBox 1.31.1。
此应用默认使用随 apps 分发的 Bash 5.3.20；ADB Shell 与应用终端的 PATH
和 Shell 并不相同。现有补充工具是 Bash、less、nano、dbclient 和 dropbearkey。
没有 apt/yum/dpkg/rpm/opkg/ipkg，也未配置可直接使用的软件包源。新增工具采用
`apps/linux-tools` 的交叉编译和版本化部署流程；不能直接安装 PC 或 ARM 软件包。

设备有两个 `top`：`/usr/bin/top` 是 BusyBox 版（当前终端 PATH 默认选它），
`/bin/top` 是 procps-ng 3.3.15 版。可运行 `/bin/top -d 1` 使用完整版并每秒刷新，
`q` 退出。若输出停住，先用 Ctrl-Q 恢复软件流控；若状态栏显示 `HISTORY`，
用“符号、符号、T”回到实时输出。PTY 持续读取输出，不需要按键才刷新。
两个版本已通过真机独立 PTY + 终端解析器的四轮更新检查；用户也确认
`top -d 1` 后显示恢复正常。离屏实际 LVGL 渲染另通过四轮无后续按键的连续刷新检查。

## 显示和兼容范围

终端为 80 列 × 14 行，每格 10×22 像素；16 px JetBrains Mono Regular/Bold
按固定格渲染，中文回退到项目共享的 Noto Sans SC，汉字占两格。底部 32 px
用于按键模式和错误提示。历史上限 200 行，解析器和输出队列有界，不持续累积
全部命令输出；无额外渲染线程。

支持常见 ANSI/VT 光标定位、插入/删除/擦除、滚动区域、SGR 16/256/真彩色、
反色/粗体/下划线/删除线、备用屏幕、应用光标模式及终端位置查询。
`vi`、`less` 等的具体版本仍需在设备上验证。UTF-8 分包、中文宽字符和组合字符
由解析器保存；复杂文字塑形、Emoji、双高/双宽行、斜体和闪烁效果不完整。
没有中文输入法、鼠标上报、剪贴板、OSC52、图像协议或终端窗口控制。
远端输出不能通过 OSC 启动本地程序或改写本地剪贴板。

原系统未确认安装 UTF-8 locale，因此子 Shell 使用 `LC_ALL=C`，输出仍按 UTF-8
渲染。中文文件名可显示；多字节光标/退格行为依赖 Shell，不能声称中文行编辑
完全兼容。若以后提供 `C.UTF-8` locale，可调整子进程环境后再验证。

## 生命周期

PTY 使用 `posix_openpt`、`setsid`、控制终端和规范行规程，窗口大小通过
`TIOCSWINSZ` 告诉 Shell。Ctrl-C/Ctrl-Z 由内核投递给前台进程组；前缀生成传统
控制字节，避免 libvterm 默认 CSI-u 与原厂旧 Shell/编辑器不兼容。

退出或 SIGTERM/SIGHUP 时关闭 PTY，并在约 200 ms 内对本终端 session 的
前台、后台任务依次发送 HUP/TERM/KILL，回收直接子进程。仅清理自己的 session；
命令主动 `setsid` 后脱离终端的服务不属于这一清理范围。所有继承的显示、输入、
锁文件 fd 在执行 Shell 前关闭。读写非阻塞，每轮输出读取最多 32 KiB，输入及
终端回复队列各最多 64 KiB。错误保留在状态栏，Shell 执行失败保留到退出。

运行需要 `/dev/ptmx` 和已挂载的 `/dev/pts`。缺失时显示错误，不自动挂载系统目录。
子 Shell 的 HOME 和历史文件位于 `$C1_APPS_DATA/terminal`，默认
`/storage/apps/data/terminal`，不修改原厂 root 的 Shell 配置。PATH 前置
`$C1_APPS_ROOT/linux-tools/bin` 和 `tools/bin`。

优先使用 `linux-tools/share/terminfo` 的 `xterm-256color`；没有则使用本目录
自带的 `c1max` terminfo（原始能力声明和编译结果均保留）。通过 SSH 连接没有
`c1max` terminfo 的远端时，可先在 Shell 设置 `TERM=xterm-256color`。

## 构建与安装接口

父级 `apps/CMakeLists.txt` 加 `add_subdirectory(terminal)` 即可。目标为
`c1max-terminal`，依赖静态 `c1vterm`、共享 `lvgl`、`m`，PTY 不依赖 `libutil`。
libvterm 的九个 C 源文件和生成表已随源码提供，无需联网获取、libtool 或 ncurses。
父项目统一提供静态 MIPS32r2/glibc 工具链和 `shared/display.cpp`、`keyboard.cpp`。

打包结构：

```text
terminal/
  c1max-terminal
  assets/                  # 整目录，包括字体许可、terminfo、inputrc、shellrc
  licenses/libvterm.txt     # 从 vendor/libvterm/LICENSE 拷贝
```

依赖 `shared/NotoSansSC-Regular.ttf`。JetBrains Mono 字体来自本机 CardputerZero
APPLaunch 已有字体包，Regular/Bold 和 `JetBrainsMono-OFL.txt` 一起保存于 assets。
libvterm 官方发布地址、版本和 SHA-256 见 `vendor/README.md`。

## 验证

运行 `tests/run.sh`：原生 C/C++，默认 ASAN/UBSAN，临时构建自动清理。覆盖 ANSI
擦除/定位/颜色、UTF-8 任意分包/宽字/组合字、备用屏幕保存恢复、历史边界、
终端查询、应用光标模式、所有 Ctrl-A…Z 和缺失符号；真实 `/bin/sh` PTY 验证
规范模式退格、14×80 尺寸、Ctrl-C 中断 `sleep` 后 Shell 继续、正常退出、错误
执行、抗 HUP/TERM 进程升级终止和 waitpid 回收。

`tools/check_mips.py` 在 builder 容器内使用既有 LVGL archive 做独立静态链接
检查，不重配父项目。`tools/inventory.sh` 是设备上可运行的只读工具/PTY清单，
不会自行调用 ADB、安装或修改系统。

`tests/render.sh /absolute/output.ppm` 使用测试显示后端，运行实际应用、真实
PTY 和 LVGL 软件渲染，产生 800×340 离屏图像；不需要设备、SDL 或浏览器。
追加 `--refresh` 可检查四轮定时 ANSI 输出产生不同完整画面，期间不发送后续按键。
已在宿主机检查英文等宽列、中文双格、粗体、下划线、256 色/真彩色和状态提示。
原生解析/PTY测试已在 macOS 及 Debian Bookworm 容器通过；静态 MIPS32r2
链接检查输出约 1.25 MB 可执行文件。这些构建产物均为临时验证文件。

以上宿主机测试不能替代设备上的键码、字体可读性、显存和全屏编辑器验证。

## Linux 工具补全依据

仓库 `docs/HARDWARE.md` 记录 BusyBox 1.31.1（385 applets）；launcher/更新脚本
已使用 `sh`、`tar`、`unzip`、`flock`，StreamPlayer 使用 GNU wget 和原厂 MPlayer。
`docs/CUSTOM-APPS-PLAN.md` 记录未找到 Python/Lua 或 dpkg/opkg/rpm。旧资料可能
来自不同批次设备，先用 inventory 核实当前板，不把库文件存在当作 CLI 已安装。

建议补全 bash（历史/行编辑）、less、nano、轻量 SSH 客户端（dbclient 或 ssh）、
jq 和 sqlite3；网络抓诊断可加 tcpdump/strace，按当前任务实际需要打包。已有
BusyBox 命令不必再覆盖原系统。统一放 apps 下的 linux-tools，避免覆盖 /bin。

Shift + 音量＋／－可将正文字号在 12–28 px 之间调整，同时更新 PTY 行列数；不改变系统音量，不增加界面提示。
