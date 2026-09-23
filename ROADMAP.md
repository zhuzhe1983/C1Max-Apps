# C1 Max 后续应用与 B 站适配方案

更新于 2026-09-23。目标：MIPS32r2 Linux、128 MB RAM、800×340、触屏与实体键盘。五子棋已完成并实测；PCSX4all 与 Processing 2D 已加入，详见各自 README。电子书、2048 和 B 站仍是规划。

## 优先顺序

| 应用 | 首版范围 | 输入与存储 | 资源目标与验收 |
| --- | --- | --- | --- |
| 电子书 | UTF-8 TXT，中文分页、字号/行距、目录、书签、自动记忆位置；第二步 EPUB 无 DRM、重排正文 | A/D 翻页，Q/E 章节，触摸设置，电源保存并回菜单；data/reader/books 与独立进度文件 | 增量读文件、只缓存当前及相邻页；应用 RSS 目标 <20 MB；大文件不整本装入内存，中文断行/重启恢复实测 |
| 2048 | 4×4、触摸滑动、撤销一步、最高分、自动保存 | WASD 或方向，电源保存并返回；data/game2048 | 只重绘变化格，RSS 目标 <12 MB；每次移动只合并一次、无有效移动不生成新块、存档可恢复 |
| 五子棋 | 15×15，本机双人＋简单 AI、悔棋、重新开始 | WASD 游标，Enter 落子，触摸选点；data/gomoku | 首版自由规则，明确不实现禁手；AI 候选点与单步时间有上限，不能阻塞电源返回；RSS 目标 <15 MB |
| B 站 | 搜索/BV 号、视频详情/分 P、收藏或观看记录、扫码授权、播放 | 设备轻量 LVGL 界面；访问自己的中转服务，保留实体键和播放器控制条 | 先通过现有播放器的连续播放验收，再做端到端原型；音视频合并/转码在服务器完成 |

内存数字是设计预算，不是实测结果。所有应用沿用 apps 独立数据目录、原子写存档、启动停止原桌面、短按电源回菜单；不添加屏幕键盘。PDF 固定版式、扫描 OCR、复杂 EPUB CSS 暂不实现。PS1 模拟器已按用户后续选择移植为 PCSX4all；自写测试程序可运行，商业游戏兼容性仍待自备镜像实测。

## GitHub 候选

| 项目 | 已核实的用途/依赖 | 对 C1 Max 的判断 |
| --- | --- | --- |
| [wiliwili](https://github.com/xfangfang/wiliwili) | C++ 完整 B 站客户端，支持触屏/键盘/手柄；扫码、搜索、收藏和播放。构建依赖 borealis、libmpv 等，许可证 GPL-3.0 | 最值得参考交互和 C++ API 层；不能把现成 PC/掌机包直接装到这台 MIPS 机器。需要重写显示平台、缩减资源，并处理播放后端 |
| [bilibili-cli](https://github.com/public-clis/bilibili-cli) | CLI 支持二维码登录和结构化搜索/视频信息；Python ≥3.10，bilibili-api-python、aiohttp、Rich 等；Apache-2.0 | 适合跑在已有服务器，向设备提供精简 JSON。不是现成播放器；不建议为了客户端引入整套 Python 运行时 |
| [biliterminal](https://github.com/teee32/biliterminal) | Python curses CLI/TUI，搜索浏览和音频，播放使用 mpv 等 | 可参考终端交互；需要 Python/curses 与播放依赖，不能直接替代本机视频后端。采用代码前必须确认明确的许可授权，公开仓库本身不等于已授权复制 |

依据：[wiliwili 构建文件](https://github.com/xfangfang/wiliwili/blob/yoga/CMakeLists.txt)、[bilibili-cli 依赖声明](https://github.com/public-clis/bilibili-cli/blob/main/pyproject.toml)、各项目 README。wiliwili 存在 CPU 视频渲染选项，因此问题不是简单的“没有 GPU 就不能运行”；主要代价是整套 UI/播放器依赖的移植和这台设备上的性能验证。

## 建议实现路线

1. 先把 StreamPlayer 新的完整帧合成路径、暂停/拖动/裁切和电源返回在真机验收，记录 CPU、可用内存与连续播放稳定性。
2. 在用户自己的服务器运行 B 站接口适配层，优先借鉴/调用 bilibili-cli 的只读功能；输出有大小上限的搜索、详情、分 P JSON。扫码登录令牌由服务端私有保存，设备只保存自己的访问凭据。
3. 服务端选择用户有权访问的流，合并分离音视频并转成 H.264 baseline/AAC、400×170、20 fps 的连续 MPEG-TS。单纯把 CDN URL 给设备无法保证分离音视频、编码、TLS 和鉴权都可播放。
4. 新建 apps/bilibili，用现有 LVGL 列表、键盘和播放器模块接入。首版不做弹幕渲染、评论发布和投稿。禁止把账号权限错误、接口风控或网络失败显示成“没有视频”。
5. 验收至少覆盖游客/扫码后、分 P、暂停/定位/停止、令牌过期、网络中断和有声无帧。API 可能变化，候选项目的现状不代表日后所有端点可用。

本轮只完成代码库调研和可执行路线，没有访问 B 站账号、架设服务、复制候选代码或承诺本机完整客户端已可用。
