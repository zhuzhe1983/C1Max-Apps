# C1Max Launcher

800×340 原生 framebuffer 应用入口，采用每页 4×2、最多 8 项的横向分页。
当前清单包括 StreamPlayer、日历、计算器、钢琴、NES、应用更新和 Terminal。
超过 8 个应用会自动增加页面；未满的最后一页不会显示虚构应用。

- 手指向左滑进入下一页，向右滑返回上一页；到达第一页或末页后不循环。
- 多页时右上角提供 44×44 像素的翻页按钮和页码，底部圆点标明位置。
- `A/D`、左/右方向键翻页，上/下方向键也可翻页。
- `Q W E R T Y U I` 分别打开当前页的 8 个位置；末页空位置不响应。
- `Enter` 打开当前选中项；换页保留位置，末页不足时选最后一个实际应用。
- `POWER`（Linux `KEY_POWER=116`）从 launcher 返回原装桌面。界面没有 Exit
  按钮，Backspace、Delete 均不触发退出。自定义应用的 POWER 应返回此菜单。

滑动必须达到 54 像素且主要沿水平方向；移动超过 12 像素、划出又划回、按下
和抬起落在不同应用、长按，均不会被误判为点击。边界上的滑动也不会打开
手指下的应用。触摸事件按单个 SYN 帧处理，避免快速按下/抬起被一起排空。

## 应用清单和图标

推荐 `apps.txt` 格式为六列，空列保留：

```text
名称|可执行文件|参数1|参数2|cwd=目录|icon-id
Calendar|/storage/apps/current/calendar/c1max-calendar||||calendar
NES|/storage/apps/current/streamplayer/c1max-streamplayer|--roms|||nes
```

兼容旧的 `名称|可执行文件|参数...|cwd=目录`，也支持明确的 `icon=calendar`
元数据。没有声明图标的旧条目会从可执行文件名和 `--roms/--updates` 推导。
清单是本机受信任配置，通过 `execv` 传参，不通过 shell 执行。过长或无效
条目被忽略；缺失或不可执行的应用显示“未安装”，点击时说明尚未安装。

源图保存于 `assets/icons/<id>.png`；构建时转换为固定 **96×96 BGRA**，每个
文件 **36864 字节**，部署路径为 `launcher/icons/<id>.bgra`。程序不依赖
PNG 解码库，支持透明通道；图标缺失或长度错误时显示备用图标。仅缓存当前
页最多 8 张图，翻页时释放离开页面的缓存。`C1L_ICONS` 可覆盖图标目录。

当前七张图标由内置 ImageGen 编辑为真实 alpha PNG，去除了原来的深蓝底色，
绘制时也不再加彩色底板。生成提示词和素材路径见
[alpha-prompts.json](assets/alpha-prompts.json)；较早的 `icon-prompts.json`
保留原始不透明版本的生成记录。选中应用使用浅薄荷色边框，按下时提供底色反馈。

标题和中英文字体改为共享的 Noto Sans SC，使用依赖中固定版本的 stb_truetype
抗锯齿绘制。字体文件只读 mmap，字形缓存上限 512 项，不加载额外字体包，
不依赖 LVGL 运行时。缺少字体时回退到旧位图字体；`C1L_FONT` 可覆盖字体路径。
配色、字号和布局规则见 [design-system/MASTER.md](design-system/MASTER.md)。

## 绘制与进程切换

界面变化时才生成完整画面，先写入不在扫描的 framebuffer 页，再提交 PAN；
静止界面不持续重绘三帧。应用退出后重新查询 framebuffer 模式，必要时恢复
进入时的 virtual geometry，再显示菜单。单页设备只做一次完整画面更新。

通过 `run.sh` 打开时停止原装 smartUI 释放内存，launcher 退出后恢复原厂。
子应用返回后清空遗留触摸与键盘事件，避免同一次 POWER 顺便关闭 launcher。
显示和输入 fd 在 exec 时关闭；TERM 会回收当前子应用进程组。前台互斥、
音量键和恢复限制见 [foreground-notes.md](foreground-notes.md)。

分页、末页选择和手势互斥逻辑可在电脑直接测试，无需连接设备：

```sh
cc -std=c11 -Wall -Wextra -Werror -DC1L_LOGIC_TEST src/launcher.c -o /tmp/c1max-launcher-logic
/tmp/c1max-launcher-logic
```

设备二进制也支持 `c1max-launcher --self-test`，该参数不会打开 framebuffer。
`c1max-launcher --render /tmp/launcher.bgra [页码]` 使用同一套绘制代码输出
800×340 BGRA，页码从 0 开始，不打开显示或输入设备，便于 QEMU 离屏验证。
构建、部署和数据布局见 [apps README](../README.md)。历史实验脚本保留在
`tools/legacy/`，不作为当前部署入口。
