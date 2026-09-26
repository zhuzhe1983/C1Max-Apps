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
