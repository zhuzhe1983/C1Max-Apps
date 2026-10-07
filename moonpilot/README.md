# MoonPilot AI

C1 Max 上基于 Moonlight / Sunshine 的远程桌面智能体。通过网络接收 H.264 桌面帧并发送键鼠操作；可配置视觉模型、文字对话、ASR 和 TTS。它与 [HID 键鼠](../hidpilot/README.md) 是两个独立应用，不需要拍摄电脑屏幕，也不使用 USB HID。

**当前为开发预览版**：配对协议测试、MIPS 构建和真机 H.264 解码通过；尚未连接用户的 Sunshine 主机，真实串流、延迟、远程输入及 AI 完整闭环仍待验证。此版本不能据此视为已完成 Moonlight 兼容性验收。

| Sunshine 主机设置 | 远程操作页（尚未连接主机） |
| --- | --- |
| ![MoonPilot 真机主机配置](../docs/screenshots/moonpilot.png) | ![MoonPilot 真机操作页](../docs/screenshots/moonpilot-desktop.png) |

## 连接电脑

1. 在电脑安装并启动 [Sunshine](https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html)，配置 Desktop 或需要的应用。
2. 在 MoonPilot「主机」输入电脑局域网 IPv4 / IPv6 地址。默认 HTTP 端口 47989；暂不支持主机名、自动发现或公网中继。
3. 点「检查主机」，首次点「PIN 配对」，将词典显示的四位 PIN 输入 Sunshine 的 PIN 页面。
4. 配对后用「切换」选择桌面或应用，再点「连接桌面」。证书和配对身份按 IP / 端口分别保存；更改主机不会复用另一台电脑的身份。
5. 返回键停止操作，电源键返回 launcher。断开串流只释放输入和本地资源，不关闭电脑上正在运行的应用。

如主机正串流另一个应用，会拒绝抢占。主机重装导致证书改变时会明确报错；确认后可删除对应 `data/moonpilot/hosts/` 目录再重新配对，不会自动忽略变化的证书。

## 画面与操作

| 画质 | 请求分辨率 | 帧率 | 目标码率 |
| --- | --- | --- | --- |
| 省流 | 512×288 | 15 fps | 900 kbit/s |
| 标准（默认） | 640×360 | 15 fps | 1500 kbit/s |
| 清晰 | 800×450 | 15 fps | 2200 kbit/s |

仅协商 H.264 8-bit 4:2:0，复用设备原厂 MPlayer / FFmpeg 解码，不引入浏览器、Java 或桌面环境。完整画面按比例显示，模型使用完整解码帧；手动点击映射排除黑边。默认在电脑播放声音，词典端不播放桌面音频。最高档是请求上限，尚未经过真实串流性能验证。

- 「手动」开启后，点画面发送左键，实体字母、数字、回车和退格发送给电脑。
- 输入任务后选「单步」或「运行 10 步」。每步重新观察画面，模型只能返回校验后的移动、点击、双击、滚动、文字、组合键或等待动作。执行后等待新画面；断流、超时、取消或非法输出会停止。
- 当前自动输入限制为 ASCII，中文需要电脑输入法；没有拖拽和多显示器布局控制。返回键和电源键由词典保留。

## 模型和语音

「设置」中四项服务分别保存完整 URL、模型名和可选 Bearer token：

| 服务 | OpenAI 兼容接口 | 用途 |
| --- | --- | --- |
| 视觉 | `/v1/chat/completions` | JPEG 桌面与 JSON 动作 |
| 对话 | `/v1/chat/completions` | 中文回答与待执行任务 |
| 识别 | `/v1/audio/transcriptions` | 16 kHz 单声道 WAV |
| 播报 | `/v1/audio/speech` | WAV 语音 |

「语音」页支持按键录音（最多 10 秒）、实体键盘文字对话和重播。电脑操作请求填入任务栏，由用户按运行开始；聊天不会自动控制电脑。TTS 失败仍显示文字回答。此前 LocalAI 测试环境的 TTS 后端故障未在本次解决，不能将接口测试视为真实播报验收。

首次启动且尚无 MoonPilot 配置时，自动复制旧 `data/hidpilot/settings.json` 的模型和语音设置，保留原文件。新设置、主机和证书仅写入 `data/moonpilot/`（文件 0600）。公共仓库不含个人服务器地址和密钥。

开发机可复制 [settings.example.json](settings.example.json) 到已忽略的 `config/moonpilot.local.json`，执行：

```sh
python3 moonpilot/tools/configure.py --serial MagicPen-931f06
```

该操作需先退出 MoonPilot，会替换它的配置文件。常规更新不会覆盖设备设置。录音在识别或取消后删除，对话只保留内存中的最近三轮。桌面图片仅在启动模型步骤时发送到配置的视觉服务器，不写相册。

## 实现、构建与测试

采用 [Moonlight Embedded](https://github.com/moonlight-stream/moonlight-embedded) 固定提交 `f32e415aea6797d261d6b470dcf8bf18727341c2` 中的 `moonlight-common-c`，结合 MbedTLS 和 ENet；PIN / HTTPS 层从其 libgamestream 协议适配。依赖由 `dependencies.json` 固定，许可证见 [licenses](licenses)。

随仓库 `tools/build.sh` 交叉编译。回归使用独立 Python TLS / PIN 协议 fixture，并非真实 Sunshine。可在 Linux 测试镜像中运行：

```sh
docker build -t c1max-moonpilot-test:bookworm -f moonpilot/tests/Dockerfile .
docker run --rm -v "$PWD:/work" c1max-moonpilot-test:bookworm sh -ec '
  cmake -S . -B .build/moonpilot-native -DCMAKE_BUILD_TYPE=Debug -DC1_BUILD_LOCAL_APPS=OFF
  cmake --build .build/moonpilot-native --target c1max-moonpilot-probe c1max-moonpilot-frame-test -j4
  python3 moonpilot/tests/protocol_test.py .build/moonpilot-native/c1max-moonpilot-probe
  .build/moonpilot-native/c1max-moonpilot-frame-test
  python3 moonpilot/tests/voice_test.py
'
```

测试覆盖双向 PIN 证明、TLS 客户端证书、服务器证书固定、错误 PIN / 证书替换拒绝、应用列表、启动参数、忙碌主机、取消、帧边界与私有权限，以及动作校验和语音 API。诊断工具 `c1max-moonpilot-probe` / `c1max-moonpilot-service-test` 不随安装包发布。

[真机与协议验证记录](../docs/2026-09-27-moonpilot-qa.md)

## 中文输入

已接入本地 Rime 拼音。适用字段、实体按键、中文搜索和草稿确认规则见[统一中文输入说明](../docs/chinese-input.md)。
