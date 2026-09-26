# 设备截图说明

首页图片均为 C1 Max 应用运行时画面，逻辑分辨率 800×340；点击缩略图可查看完整图片。

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

原有 StreamPlayer、日历、计算器、钢琴、终端、五子棋、NES、PCSX4all、DOSBox、Processing 截图保留。PCSX4all 展示用户提供游戏的运行画面；NES 当前是设备渲染/输入自测；DOSBox 展示随项目提供的 DOS LAB。游戏镜像不在仓库中。

## Bilibili（2026-09-26 至 09-27）

- `bilibili.png`：真机渲染真实 API 目录缓存，三个封面；底栏标明缓存刷新。
- `bilibili-playback.png`：真实公开视频原始 360p MP4 的本地静音播放测试，未转码，展示 Fit 与控制条。
- `bilibili-search.png`：实体输入、Backspace 删除后的搜索页。

这三张均为设备 framebuffer 截图。因设备 Wi-Fi 未连通，目录缓存由电脑抓取后写入独立 QA 目录；播放截图不是网络端到端播放证明。没有登录凭据，临时视频不随仓库发布。详情见 [Bilibili QA](../2026-09-26-bilibili-qa.md)。
