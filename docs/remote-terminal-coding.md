# 远程 Terminal Coding 方案

这个项目的目标是让 C1Max 做一个可靠的输入、显示和网络终端，把 Coding Agent 的运行时放在家里的常驻 Linux 主机上。C1Max 的 MIPS CPU、内存和电池只承担 SSH、终端渲染、中文输入和语音输入，不在设备上运行 Node.js、Rust Agent 或 Herdr。

## 已确定的架构

```text
C1Max Terminal
  └─ Dropbear SSH client ──> home-dev-vm / 其他 Linux 主机
                              └─ Herdr server
                                  └─ Codex / Claude / Pi / 其他 CLI Agent
```

Herdr（[herdrdev/herdr](https://github.com/herdrdev/herdr)）提供服务端持久 PTY、detach/reattach、工作区、窗口和 Agent 状态识别。它是一份 Linux/macOS 单体二进制，适合放在服务器；C1Max 是 MIPS 架构，且设备端不需要这些编排能力，因此不把 Herdr 打进设备镜像。

可选择常驻 Linux 开发主机或 NAS 上的 Linux VM，使用服务管理器运行 Herdr；实际主机地址与模型凭据保存在设备／服务器配置中。

## 设备端用法

设备 Terminal 的 `agent` 命令只负责建立带 PTY 的 SSH 连接：

```text
agent user@server
agent user@server project-name
agent configure user@server project-name
agent
```

目标主机需要先安装 Herdr 并让 `herdr server` 由 systemd 或其他正式服务管理。第一次进入时，Herdr 会创建或接入指定名称的持久会话。按 `Ctrl-B` 后按 `Q` 分离，服务端 Agent 继续运行；重新执行 `agent` 即可接回。直接在远程窗格输入 `exit` 会结束那个窗格的 shell，暂离请使用 Ctrl-B、Q。


如果不使用 Herdr，仍可直接使用 `ssh -t user@server`；`agent` 只是把常用的持久 Coding 工作流固定下来。

出站 SSH 使用 Dropbear 格式私钥。先运行 `mkdir -p "$HOME/.ssh"`，再用 `dropbearkey -t ed25519 -f "$HOME/.ssh/id_dropbear"` 生成设备密钥，把输出的公钥安装到服务器。包装脚本只自动选择经过 `dropbearkey` 验证的私钥；普通 OpenSSH 私钥需要先在电脑上用 `dropbearconvert openssh dropbear` 转换，改文件名不能转换格式。显式指定 `ssh -i FILE` 时保留用户的选择。

## 电源策略

设备端新增轻量 `c1max-power-guard`，由 launcher 的监督器启动，退出至原厂桌面时停止。它通过 vendor `PowerManager` 的 `/dev/socket/PowerLock` 建立 per-client `suslock`：有入站 SSH 会话、Terminal 启动了 `dbclient/ssh/scp`，或者设备接入外部电源且 Dropbear 正在监听时持有锁；SSH 断开且设备改用电池后立即释放。协议帧是 NUL 结尾的 `Register suslock <pid>`，服务端必须返回 `ok` 才算成功，守护每 5 秒重新确认一次。这样插电时可以从远端随时建立 SSH，电池模式仍允许系统深度休眠，电源键可以正常唤醒。

屏幕熄灭计时仍由设置页控制，默认不强制“永不熄屏”。建议 Coding 场景选择 5–10 分钟：屏幕可以省电，活动 SSH 由 suspend lock 保持网络和会话；没有活动连接时设备按系统策略休眠。

## SSH 恢复策略

先在设置页或用 `sshd password` 设置自己的密码，或用 `sshd authorize FILE` 导入公钥，再启动服务。没有统一默认密码；已存在但损坏、不安全或缺失的认证文件不会触发重置为已知密码。SSH 只在用户显式启用时监听。

## 验证范围

主机端会话管理器由用户自行部署。PR 贡献者曾报告过真实设备的语音录音、SSH 和 PowerLock ACK 测试；审查修订后的版本通过主机端协议、界面和 MIPS 构建测试。本次审查没有部署设备或私有服务器，因此真实麦克风、PowerManager 重启、Wi-Fi 恢复和长时间电池消耗仍需在设备上复核。

项目不把任何私有主机地址、模型密钥、SSH 私钥或服务密码编译进设备包；服务器目标由用户在设备上通过 `agent configure` 自行设置。
