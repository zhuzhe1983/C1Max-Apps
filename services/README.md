# C1Max Terminal 语音适配器

`terminal_voice_gateway.py` 把 C1Max Terminal 的 JSON 协议适配到已有的局域网 ASR/LLM 服务：

- `POST /v1/terminal/voice` 接收 Terminal 的 base64 WAV 请求。
- 上游 ASR：原始 `audio/wav` POST 到配置的识别端点，预览返回 `partial_text`。
- 最终请求可调用 Qwen 的 OpenAI 兼容 `/v1/chat/completions`，使用窗口内容做有限润色。
- `GET /health` 返回适配器状态，不返回密钥。

在承载 ASR/LLM 的 Linux 主机安装：

```sh
sudo useradd --system --home-dir /opt/c1max-terminal-voice --shell /usr/sbin/nologin c1voice  # 只需一次
sudo install -d -o c1voice -g c1voice /opt/c1max-terminal-voice
sudo install -m 755 terminal_voice_gateway.py /opt/c1max-terminal-voice/terminal_voice_gateway.py
sudo install -m 600 terminal-voice-gateway.env.example /etc/c1max-terminal-voice.env
sudo install -m 644 terminal-voice-gateway.service /etc/systemd/system/c1max-terminal-voice.service
# 启动前编辑 /etc/c1max-terminal-voice.env，填写本机实际服务地址与模型名。
# LLM_API_KEY_FILE 指定的文件需对 c1voice 可读，建议所有者 c1voice、权限 600。
sudo systemctl daemon-reload
sudo systemctl enable --now c1max-terminal-voice
curl http://127.0.0.1:9896/health
```

把 env 示例中的 `ASR_URL`、`LLM_URL`、`LLM_MODEL` 和 `LLM_API_KEY_FILE` 改成实际服务值。设备 Terminal 的服务地址填 `http://<lan-host>:9896/v1/terminal/voice`，模型名可填 `terminal-gateway`；如果设置了 `AUTH_TOKEN`，设备 API 密钥填写同一个值。模型服务的 key 只在适配器主机读取，不下发到设备。
