# 2026-10-07 本机验证

实际在 macOS Apple Silicon 的 QEMU/Malta、128 MB MIPS Linux 客体中执行。结果见 [results.json](results.json)。

- 独立完整构建入口通过；编译缓存从已有公开系统构建迁入独立 volume，本次未执行空缓存全量下载验收。
- 七个官方 MIPS 包均启动；计算器客体 ELF 与官方下载包的 SHA-256 一致。
- [数字运算](calculator-result.png)：设备键码输入 12+3，实际显示 15。
- [终端输入](terminal-input.png)：evdev 输入小写字母和数字，shell 实际回显 abc 123。
- [原始商店安装](appstore-installed.png)：HTTPS 刷新与设置包下载、校验、安装成功。
- [网页按需安装](mail.png)：邮件包写入原生商店 generation，应用运行与菜单记录通过。未配置邮件账号或执行收发验收。
- 重启保留设置安装和测试文件；正常关机日志确认 ext4 卸载，qemu-img check 无错误。
- 钢琴键盘/触摸与客体 MP3 解码录得非零 48 kHz 立体声；音频协议的暂停/恢复/位置查询通过。录音留在被 Git 忽略的 build/verification/audio.wav。
- [NES 浏览器](nes.png)：官方 NES 包按需安装并以 --roms 启动原始浏览器；未导入或运行游戏 ROM。
- Chrome 实际显示彩屏、图标、键盘和应用目录；日常实例使用 CoreAudio。

后续 [Bilibili 播放验证](bilibili.md) 覆盖了 MIPS H.264/AAC 视频、声音、暂停、跳转及线上 403 修复。

Windows/Linux 主机、摄像头、USB HID、真实 WLAN 与未列出的外部账户服务尚未验收，不能从 UI 启动推断这些功能可用。
