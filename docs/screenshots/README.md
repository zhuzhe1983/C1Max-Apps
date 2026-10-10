# 设备截图说明

首页图片均为 C1 Max 应用运行时画面，逻辑分辨率 800×340；点击缩略图可查看完整图片。

## Tox（2026-09-25 采集，2026-10-02 公开）

- `tox.png`：词典上两个专用测试身份的本机回环聊天记录；截屏时对端进程已退出。
- `tox-add.png`：添加好友页的测试输入，未发送请求。
- `tox-home.png`：当天公共 Tox 网络的 TCP 连接状态，不代表节点现在仍然可达。
- `tox-qr.png`：隔离测试身份的公开 ID 与二维码，测试后已删除该身份资料。
- `tox-qr-confirm.png`：镜头识别专用测试二维码后的确认页。

均为真机 framebuffer 抓帧；不含用户的身份、私钥或真实聊天记录。更多说明见 [Tox](../../tox/README.md)。

## 原有应用截图

本次新增 Airtune、CrossPoint、拍立得、邮件，以及更新后的 launcher 两页。除下表注明的既有相机验收图外，本次使用部署版本 `20260924-205817-65db3bee` 的 `c1max-capture` 读取 framebuffer；只将 340×800 BGRA 转换、旋转为横屏 PNG，没有重绘或拼接应用界面。

| 文件 | 内容与来源 |
| --- | --- |
| `launcher.png` / `launcher-apps.png` | 正常 launcher 的第一页、第二页，共 15 个入口。 |
| `airtune.png` | SAVED 本地列表及内置电台，未启动音频播放。 |
| `crosspoint.png` | 原创《午后小记》演示 EPUB；不是用户下载的书籍。 |
| `camera-preview.png` | 复用 2026-09-24 相机状态栏修复后的真机验收图（`camera-status-qa/landscape.png`），展示墙面取景、复古滤镜与白相纸；不是这轮补图时重新拍摄。 |
| `camera.png` | 本次相纸选择层截图，拍摄环境较暗，因此预览缩略图接近黑色；使用隔离设置，未拍照或改动用户相册。 |
| `mail-inbox.png` / `mail.png` | 空收件箱、未发送的演示草稿；无真实邮箱配置或通信。 |

本次应用截图操作使用临时应用数据目录，完成后清理。图像不包含个人服务器地址、访问令牌或邮箱凭据。没有将演示邮件包装成真实收发验证。

原有 StreamPlayer、日历、计算器、钢琴、终端、五子棋、NES、PCSX4all、DOSBox、Processing 截图保留。PCSX4all 展示用户提供游戏的运行画面；NES 原自测图于 2026-10-10 替换为《坦克大战》实机画面（见下）；DOSBox 展示随项目提供的 DOS LAB。游戏镜像不在仓库中。

## Bilibili（2026-09-27 联网复测）

- `bilibili.png`：真机直接连接 B 站 API 后的热门目录，三个在线封面。
- `bilibili-playback.png`：B 站 CDN 原始 360p MP4 的真机网络播放，暂停展示完整画面与控制条，未转码。
- `bilibili-search.png`：实体按键事件输入 processing 后的真实联网搜索结果。

这三张均为设备 framebuffer 截图，替换了首版的离线验证图。数据使用独立 QA 目录，没有账号凭据；临时视频不随仓库发布。详情见 [Bilibili QA](../2026-09-26-bilibili-qa.md)。

## Bilibili 横竖屏与直接切换视频（0.2.0）

- `bilibili-portrait-list.png`：真实热门数据按宽高筛出的竖屏精选分类。
- `bilibili-portrait.png`：视频、标题和触摸控件一起右转 90°，控制区在横放设备的左侧。
- `bilibili-portrait-upright.png`：上一张的原生 340×800 方向，便于竖握阅读；没有重新绘制界面。
- `bilibili-next-hidden.png`：点击下一条后直接播放新视频，控件保持隐藏。

均来自独立 QA 会话的真机 framebuffer，使用公开 B 站视频，没有登录账号。只做 BGRA 到 PNG 的颜色格式转换及阅读方向旋转。视频未转码、未下载到仓库。验证记录见 [横竖屏 QA](../2026-09-27-bilibili-portrait-qa.md)。


2026-09-27 新增 `hidpilot.png` 和 `hidpilot-voice.png`，通过设备 framebuffer 当前扫描页采集，旋转为 800×340。键鼠页为真实 HID 会话界面；对话页文字由真实 LocalAI 模型返回，输入由测试工具经设备输入事件送入。没有录制、拼接或伪造语音识别对话，也不表示当前 TTS 服务已验收。摄像头拍到私人环境的预览不上传公开仓库。


2026-09-27 拆分 HIDPilot 后新增 `moonpilot-launcher.png`：已安装两个新应用后，从真实设备 framebuffer 采集专用的两项测试菜单，仅旋转为 800×340。它不是完整应用列表，也不是已连接 Sunshine 的截图。连接恢复后，`hidpilot.png` 更新为 0.2.0 的真实 USB 已连接状态；新增 `moonpilot.png` 主机页、`moonpilot-desktop.png` 未连接时的操作页，均读取真机 framebuffer。`hidpilot-voice.png` 仅保留为 0.1.0 历史对话截图；AI 和语音现已迁入 MoonPilot。尚无真实 Sunshine 串流截图。

## 设置 0.2.0（2026-10-02）

- `settings.png`：显示与熄屏页，亮度 −／+ 触摸按钮。
- `settings-picker.png`：自动熄屏的整行选项列表，左上角「返回」按钮。
- `settings-sound.png`：媒体音量（`softvolume`）与输出设备。
- `settings-about.png`：本机信息首屏（型号、处理器、内存、存储）。
- `settings-wifi-add.png`：添加隐藏网络，网络名称为测试输入，密码为测试字符，未提交连接。

均为真机 framebuffer 截图，只做 BGRA 到 PNG 转换和横屏旋转。按键和触摸由设备输入事件注入。WLAN 列表和「关于」页后半部分含本机 SSID、IP、MAC 和序列号，没有收录。截图时临时改过的亮度和熄屏时间都已恢复。

## Tox 多媒体与应用商店（2026-10-02）

`tox-voice.png` 是录音完成、尚未发送的真机界面；`tox-background.png` 展示开启后台后的界面，均使用两个本地回环测试身份，不含真实联系人。相机画面和实际录音没有上传。

`appstore.png` 与 `appstore-detail.png` 来自真机的隔离商店配置，展示按需安装目录和首页可见性控制。测试隐藏／卸载的是独立 QA 清单中的记录，没有删除用户资料。

`appstore-installed.png` 是真机从公开 GitHub Release 下载并安装 Tox 0.2.0 后的详情页，使用同一隔离商店配置；不是电脑端界面预览。

## Tox 离线队列（2026-10-02）

`tox-outbox-chat.png` 和 `tox-outbox.png` 来自词典 framebuffer，展示实体键盘输入的离线消息，以及待发送／未确认项的不同操作。全部使用隔离双身份测试资料，不含真实联系人或私人聊天。截图只转换像素格式并旋转为正常屏幕方向。

2026-10-03 更新为气泡布局，新增 `tox-bubbles.png` 与 `tox-inline-media.png`：两个隔离身份实际通过 Tox 互发消息、JPEG 与 WAV。灰勾截图时暂停了专用收件进程，绿色圆勾来自实际回执；截图后恢复测试进程。附件内容为本仓库 Tox 图标和生成的静音 WAV，未使用私人照片或录音。

`tox-store-updated.png` 为 2026-10-03 真机通过 GitHub 应用商店下载 Tox 0.3.0 后的完成页面。

2026-10-07 更新 `piano.png`：C1 Max 运行《致爱丽丝》主题简编的实际 framebuffer，显示自动演奏高亮与后台开关。测试保持系统静音。

## NES 0.2.2（2026-10-10）

`nes.png` 为 C1 Max 实际运行用户 NAS 中的《坦克大战》第一关的 framebuffer，左右显示实体按键提示。本次运行由 NES 0.2.2 生产目标文件加只读 PCM 统计包装器链接，使用隔离 QA 菜单，用于核对音频恢复；渲染和游戏核心与正式版本一致。只转换 BGRA 并旋转为 800×340 PNG，没有重绘、拼接。ROM 和测试包装器均不在仓库或更新包中。
