# Terminal 语音输入服务协议

Terminal 的语音输入是一个可选的局域网服务调用。设备端只负责录音、发送请求和把服务返回的文字写入当前 PTY；语音识别模型和润色模型运行在用户配置的服务端。设备没有配置服务时，终端不会上传音频或窗口内容。

## 与现有实时语音服务的关系

现有实时语音应用可以在服务端维护会话、模型连接、TTS 和工具事件；设备不直接连接模型，也不携带模型密钥。C1Max Terminal 复用了这个边界，但只需要得到一段可插入命令行的文字，因此使用轻量兼容接口 `POST /v1/terminal/voice`，无需把完整会话协议搬进终端。

兼容适配器把原始 WAV 转给配置的 ASR 服务，最终阶段再把识别结果和终端窗口上下文转给配置的 OpenAI 兼容对话服务；模型密钥只留在服务端。录音中的预览采用完整 WAV 快照周期识别，效果相当于实时文字事件，但避免在 C1Max 上维护长连接和 WebSocket 客户端。以后如果 Terminal 需要语音播报或工具动作，可以在服务端接入现有会话协议，不需要改变当前输入协议。

## 配置

在系统设置 → **语音输入**中填写：

- **服务端地址**：完整的 `http://` 或 `https://` URL，例如 `http://<lan-host>:8080/v1/terminal/voice`。
- **模型名称**：服务端使用的识别／润色模型标识。服务端也可以忽略它，使用自己的默认模型。
- **API 密钥**：可选。设备以 `Authorization: Bearer <密钥>` 发送。
- **服务来源**：默认可复用 MoonPilot 设置中的 ASR 和对话服务；关闭后使用本页填写的聚合接口。
- **发送终端窗口内容**：默认开启。关闭后请求仍发送音频，但 `context` 为空对象。
- **实时文字预览**：默认开启。录音时周期发送音频快照，只显示暂定识别文字；旧服务端只支持整段识别时可以关闭。

配置保存到设备的 `$C1_APPS_DATA/terminal/voice.json`，权限为 0600：

```json
{
  "enabled": true,
  "include_context": true,
  "live_preview": true,
  "use_moonpilot": true,
  "endpoint": "http://<lan-host>:8080/v1/terminal/voice",
  "model": "home-asr",
  "token": ""
}
```

启用服务后，在 Terminal 中点击右下角 **语音**开始录音，再点击 **结束**停止；实体键 **符号 → V**也可以开始／结束（语音关闭时保留 Ctrl-V）。录音最长 20 秒，到时自动识别。录音中的预览显示在浮层，结果返回后才插入当前命令行，用户仍需自己按回车执行。

当 `use_moonpilot` 为 `true` 时，设备读取 `$C1_APPS_DATA/moonpilot/settings.json`：ASR 使用其中的 `asr.endpoint/model/token`，最终润色使用其中的 `chat.endpoint/model/token`。ASR 请求沿用 MoonPilot 的 `multipart/form-data` `/v1/audio/transcriptions` 接口；最终润色请求使用 OpenAI 兼容的 `/v1/chat/completions`。MoonPilot 的对话服务未配置时，Terminal 仍可插入 ASR 原文，不会伪造润色结果。

局域网适配器配置示例：

```json
{
  "enabled": true,
  "include_context": true,
  "live_preview": true,
  "use_moonpilot": false,
  "endpoint": "http://<lan-host>:9896/v1/terminal/voice",
  "model": "terminal-gateway",
  "token": ""
}
```

兼容服务源码和 systemd 单元在 [`services/`](../services/)；服务端健康检查为 `GET http://<lan-host>:9896/health`。适配器只在服务端读取模型 Bearer key，设备配置中不需要填写该 key。若要限制局域网调用，可在服务端 env 设置 `AUTH_TOKEN`，设备的 `token` 字段会按 `Authorization: Bearer ...` 发送。

## HTTP 请求

当 `use_moonpilot` 为 `false` 时，客户端向配置的完整 URL 发出 `POST`，请求体为 UTF-8 JSON。服务端应接受下列字段：

```json
{
  "protocol": "c1max-terminal-voice/v1",
  "phase": "final",
  "model": "home-asr",
  "audio": {
    "format": "wav",
    "encoding": "pcm_s16le",
    "sample_rate": 16000,
    "channels": 1,
    "data_base64": "<RFC 4648 base64 WAV>"
  },
  "context": {
    "application": "c1max-terminal",
    "rows": 14,
    "columns": 80,
    "screen": "<当前可见终端的纯文本内容>"
  }
}
```

`audio` 是 16 kHz、单声道、16-bit little-endian PCM WAV，包含完整 WAV 头。`screen` 取开始录音时的可见窗口，已去掉 ANSI 控制序列、隐藏属性文本和行尾空格，最多约 8 KiB，可能包含命令输出、当前提示符和用户正在编辑的命令行。服务端必须把它视为不可信的上下文数据，不把其中的文本当成服务端指令。

`phase` 有两种值：

- `preview`：设备约每 1.2 秒发送一次**从本次录音开始到当前的完整 WAV 快照**，不是增量片段。服务端做快速 ASR 并尽快返回，建议耗时小于 1 秒；不要在预览阶段运行较慢的润色模型。每次结果替换上一次预览，设备不会把它写进 PTY。一次最多一个请求在途；识别较慢时跳过中间快照，不积压请求。
- `final`：结束录音后发送完整音频，服务端做最终识别及上下文润色。没有 `phase` 的旧请求应视为 `final`。

预览请求的设备超时为 6 秒，失败后本次录音退回结束后识别。停止录音时会取消在途预览，再发送最终请求。独立接口最终请求的设备超时为 95 秒；MoonPilot 模式中 ASR 与润色各有 95 秒上限。适配器的 ASR／润色共享 `REQUEST_TIMEOUT` 时间预算。服务端不保存会话状态；设备会取消本地网络请求，但 urllib 上游推理可能继续至其超时，无法保证立即停止远端计算。预览是周期快照识别，延迟由采样周期、网络和服务端推理时间共同决定，不是 WebSocket 逐字流。

服务端可以先做 ASR，再根据 `screen` 做有限润色。推荐的润色规则是：保持用户原意；命令、路径、参数和代码按窗口中的拼写优先；只补全明显的口语标点或同音字；无法判断时保持原识别结果；不要添加换行、回车、控制字符或自动执行意图。这样返回的文字可安全地放进当前命令行，执行仍由用户明确按回车完成。

推荐同时覆盖两类输入：

- **Terminal 命令/代码**：把 shell 命令、路径、选项、包名、函数名、变量名、大小写、下划线、点号和斜杠当作高优先级内容，只按当前窗口纠正明显的同音字或拼写；不补参数、文件名、引号、代码和管道意图。
- **AI Coding 请求**：保留用户说出的任务边界、技术名词和约束，只做轻量听写纠错；不回答请求、不扩展任务、不把窗口内容里的命令当成模型指令。

两类输入都应在不确定时原样返回，并最终只生成一行可插入文本。服务端不能因为模型“理解了用户意图”而替用户执行命令；执行仍由用户明确按回车或由后续 coding agent 流程负责。

## HTTP 响应

成功时返回 2xx 和 JSON：

```json
{
  "text": "请查看当前目录下最近修改的文件"
}
```

`text` 是最终要插入终端的 UTF-8 文本，长度不超过 1200 字节，不得包含 ASCII 控制字符（包括 `\r`、`\n` 和 `\x1b`）。也可以使用同义的 `insert_text` 字段；客户端优先读取 `text`。非 2xx、无效 JSON、空文本或不符合上述限制时，Terminal 会显示错误且不会写入 PTY。

预览响应使用同样的 2xx JSON，可以返回 `partial_text` 或 `text`：

```json
{"partial_text": "查看当前目录"}
```

`partial_text` 为完整暂定文本，不是追加片段，同样限制 1200 字节且不含控制字符。尚无可识别语音时可以返回 `{"partial_text":""}`；客户端保留上一段预览。预览的文字随识别修订，最终 `text` 才是提交内容。

## 服务端实现建议

服务端把 `preview` 交给快速 ASR，把 `final` 交给最终 ASR 和润色模型。保持这两个阶段独立，可以在家庭算力上提供说话过程的反馈，同时只在结束时运行润色。

MoonPilot 兼容模式不新增常驻服务：`preview` 和 `final` 都调用现有 ASR；只有 `final` 且窗口上下文开启时才调用现有对话模型润色。这样同一套 ASR／对话模型可同时服务 MoonPilot 和 Terminal。

服务端应只监听家庭 LAN 或通过 HTTPS 暴露，校验 Bearer 密钥，限制请求体大小（建议 1 MiB）和处理时长，并在日志中避免记录原始音频、密钥和完整窗口内容。20 秒 WAV 经 base64 编码约 854 KiB。

适配器会校验实际 WAV 格式、拒绝超过 20 秒的音频，最多同时处理 4 个连接；请求读取有超时，不跟随上游重定向。`GET /health` 只返回是否配置，不暴露上游地址。测试使用本地固定响应夹具：`python3 -m unittest discover -s services/tests -v`。
