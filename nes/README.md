# NES / 红白机

基于 InfoNES 的 C1 Max 原生前端，使用 Nes_Snd_Emu / Blip_Buffer 合成 NES 五声道。把自己的 `.nes` 文件放在 `/storage/apps/data/nes/roms/`，从 Launcher 的 NES 入口选择。仓库和应用包不包含游戏 ROM。

W/S/A/D 为方向，J/K 为 A/B，Q 为 Select，E 或回车为 Start；触摸按键也可用。电源键短按提示，长按五秒退出。音量键由 Launcher 的全局助手处理，长按音量 − 到三秒静音。

## 0.2.1 声音修复

- 替换旧 pAPU 的波形合成，修复原先包络／扫频不工作、噪声序列和声道音量比例不正确造成的音色问题。新声音按累计 CPU 周期处理寄存器写入，提供带限采样和直流滤波。
- tinyalsa 直接输出前读取系统 `softvolume`，按左右声道缩放；启动和音量变化使用约 5 ms 渐变。零值静音，读取失败也静音，不重复修改全局音量。
- 完整处理 PCM 短写，中断和欠载恢复有次数上限。声卡失败时继续静音游戏，每帧限速，避免旧实现每条扫描线等待 16 ms。
- APU 状态读取和帧计数器接入新声音核心，移除旧 `$4015` 数组越界访问。

支持基本 NTSC 五声道。仍使用 InfoNES 的 CPU／PPU；总线时间是指令级，未模拟 DMC 取样造成的 CPU DMA 停顿，也未增加扩展声源或 PAL 模式。声音改善不等于整机逐周期精确仿真；扬声器和原电视音箱也会有差异。

## 构建与验证

先执行 `python3 tools/fetch_deps.py` 获取固定依赖，完整构建见项目根 README。

- `bash nes/tests/run.sh`：真实 PCM 输出代码，ASan/UBSan 与模拟声卡故障。
- `bash nes/tests/run-apu.sh`：真实声音核心的音高、包络、噪声模式、状态／IRQ 和 DMC 回归。
- MIPS 构建目标 `c1max-nes-audio-test`、`c1max-nes-apu-test`、`c1max-nes-core-test`：输出、声音合成、实际 CPU 接口与 RGB555 显示回归。

许可证与可重新链接的源码说明见 [licenses](licenses/NOTICE.txt)，实测记录见 [2026-10-10 排查记录](../docs/2026-10-10-nes-audio-qa.md)。
