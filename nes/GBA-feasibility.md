# 快易典 C1 Max (MP-D350MAX) 跑 GBA 模拟器可行性调研

> 目标硬件：Ingenic **X2000**（双核 XBurst II V2，**MIPS32r5**，1.1–1.5GHz，128-bit MSA，无 3D GPU），
> 128MB 物理 / ~100MB 可用，Linux 4.4.94 / glibc / mipsel 静态，fb2 软绘 800×340，ALSA(ES8326)，已 root。

---

## 0. 结论先行（TL;DR）

**可行，且比想象中乐观。属于"可行，需做移植但无需从零写 dynarec"这一档，难度中等。**

核心论据：**gpSP 早就有成熟的 MIPS32 动态重编译（dynarec）后端**，正是为 Ingenic MIPS 掌机（GCW Zero / OpenDingux / RetroFW / RS-90 等）而生。这些设备用的 **JZ4770（单核 XBurst1 @1GHz，MIPS32r2）就能把绝大多数 GBA 游戏跑到接近满速**。C1 Max 的 X2000 是**更新一代的 XBurst II（MIPS32r5，向下兼容 r2），单核性能≥JZ4770，主频还更高**，所以 **X2000 上跑 GBA 满速/接近满速是有充分先例支撑的**。

现成 MIPS dynarec 后端存在 → 不需要自己写编译器后端（这是 GBA 移植最难、最容易劝退的部分），X2000 的工作量退化为"给一个已有的 mipsel GBA 模拟器换一套 framebuffer + ALSA + 触摸输入的 I/O 层"。

唯一真正需要关注的点是**内存**（100MB），但 gpSP 当年就是为 **32MB RAM 的 PSP** 设计的、带 ROM 按需分页，100MB 绰绰有余。

---

## 1. 性能可行性（量化估计）

### 参照系：和 X2000 同族的 Ingenic MIPS 掌机实测

| 设备 | SoC | 核 / 架构 | 主频 | RAM | GBA 模拟器 | 实测表现 |
|---|---|---|---|---|---|---|
| **GCW Zero** | **JZ4770** | 单核 XBurst1 / **MIPS32r2** | 1.0 GHz | 512MB | ReGBA (gpSP 0.9) | **绝大多数游戏满速**；Golden Sun 2 修道院最低仅掉到 51fps、战斗基本 frameskip 0；仅 *Payback*（软件 3D）约 14fps 不可玩 |
| RS-97 | JZ4760 | 单核 XBurst1 / MIPS32r2 | ~1.0 GHz | 32MB | gpSP | 可玩，多数游戏良好（UI/按键映射较糙） |
| **C1 Max（本机）** | **X2000** | **双核 XBurst II / MIPS32r5** | **1.1–1.5 GHz** | 128MB | 待移植 gpSP/ReGBA | **推断：≥ GCW Zero，绝大多数游戏满速 60fps** |

### 为什么 X2000 只会更好

- **单核性能**：XBurst II 是**双发射（dual-issue）**乱序改进核，IPC 高于 JZ4770 的 XBurst1；主频还高 10–50%。gpSP 是**单线程**，只吃一个核，但 X2000 单核就已经 ≥ JZ4770 单核。
- **BogoMIPS≈2200** 与 JZ4770 同量级甚至更高，说明整数吞吐充足。
- GBA = ARM7TDMI @16.78MHz + 2D PPU（无 3D），dynarec 把 ARM 码翻成本地 MIPS 码后，**CPU 端仿真几乎"零成本"**，瓶颈通常在 PPU 逐行软件光栅化 + 显示缩放 blit，而这些 JZ4770 都扛住了。

### 量化估计

- **纯解释执行（VBA-M/无 dynarec）**：在 1.1GHz MIPS 上，重场景 GBA 大概只有 **15–35fps**，多数 RPG 勉强、动作游戏偏卡——**不推荐**。
- **带 MIPS dynarec（gpSP/ReGBA）**：**90%+ 游戏满速 60fps 或 frameskip 0**；少数软件 3D/超重特效游戏（Payback、部分 Mode7 重缩放）可能需要 frameskip 1 或落到 40–50fps。**总体"可玩"目标（40+，多数满速）稳达。**
- 对显示的额外开销：GBA 240×160 → 需缩放到 ~510×340（适配）或整数 2× 到 480×320（超屏裁切）。软件缩放这部分开销要计入，但 fb2 软绘 + 简单点采样/整数放大在 1.1GHz 上可控（InfoNES 已验证 fb 通路可行）。

---

## 2. 模拟器选型对比

| 模拟器 | 类型 | MIPS dynarec | 内存 | 精度 | 适配 X2000 难度 | 评价 |
|---|---|---|---|---|---|---|
| **gpSP (libretro/gpsp, davidgfnet 维护)** | dynarec | ✅ **MIPS32(r2)/MIPS64 原生后端** | 极低（为 PSP 32MB 设计） | 中（够玩） | 中 | **首选内核**。活跃维护、后端全（x86/ARM/ARM64/**MIPS32/64**）、ROM 分页 |
| **ReGBA (retrofw/regba, gpSP 0.9 衍生)** | dynarec | ✅ **MIPS32r2（GCW0/RetroFW 实测）** | 极低 | 中 | **中偏低** | **移植路线首选**：本身就是**独立 SDL 应用**（自带 UI/按键/frameskip/rewind），有 `Makefile.gcw0`，改 I/O 即可 |
| **picogpsp (shauninman/neonloop)** | dynarec | ✅ 继承 gpsp MIPS 后端 | 极低 | 中 | **低** | **独立 libpicofe 前端**（不需要 RetroArch），libpicofe 支持 **framebuffer/SDL**，正是为 TrimUI/Miyoo 这类小掌机做的——**最贴合 fb2 裸机场景** |
| TempGBA / gpSP-Kai | dynarec | ✅ MIPS（DSTwo/OpenDingux） | 低 | 中 | 中 | ReGBA 的近亲/上游，兼容性≈ReGBA 1.45 |
| VBA-M / VisualBoyAdvance | 纯解释 | ❌ | 中 | 高 | 低（但慢） | **性能不达标**，仅作对照 |
| mGBA | 解释（有实验 JIT） | ❌ 无成熟 MIPS JIT | 较高 | **最高** | 高且慢 | 精度最好但重，1.1GHz MIPS 解释跑不满速，**不推荐** |
| gpsp-rs (Rust 重写) | dynarec（早期） | ❌/不成熟 | - | - | 高 | 不成熟，pass |

**推荐组合**：内核用 **gpSP MIPS dynarec**，前端/工程用 **picogpsp（libpicofe，最省事，天然 fb 化）** 或 **ReGBA（独立 SDL，功能全）**二选一。

---

## 3. MIPS dynarec 现状（关键问题的答案）

> 问题原文："gpSP 的 dynarec 是 ARM/x86 后端，**MIPS 后端存在吗**？"

**存在，而且是一等公民、久经考验。**

- gpSP 官方支持后端：**x86/x64、ARMv6/7、ARMv8，以及 MIPS（32 位与 64 位）**。MIPS32 后端由 **Exophase 在 gpSP 0.9 引入**，就是为 Dingux/OpenDingux 的 Ingenic MIPS 掌机写的。
- 该后端针对 **MIPS32r2**：使用 `EXT/INS/ROTR/ROTRV` 等 r2 位操作指令（社区在把它移到只有软件模拟这些指令的旧核时才会变慢——反过来说明它**默认吃 r2 硬件指令**）。
- **X2000 是 MIPS32r5，官方明确"向下兼容 MIPS32r2"**，且 XBurst ISA 完全兼容 MIPS32 → **现成的 gpSP MIPS32r2 dynarec 二进制/代码可直接在 X2000 上原生运行**，无需改指令生成器。
- dynarec 是**标量整数重编译**，**不依赖 MXU/MSA SIMD**。所以 JZ4770 的 MXU 与 X2000 的 MSA 差异**对 gpSP 完全无影响**（GBA 仿真本身不需要 SIMD）。这消除了一个常见的跨 Ingenic 移植坑。

**移植到 X2000 的 dynarec 难度：几乎为零**——不是"要不要写 MIPS 后端"，而是"现成 MIPS 后端能不能编过 + 跑起来"，答案是能。真正要动的只有平台 I/O 层（见 §7）。需注意的运行时细节：dynarec 需要 **可执行的 JIT 内存**（mmap `PROT_EXEC` + 翻译后 `cacheflush()` 刷 I/D cache），已 root 的 4.4 内核默认允许；从 VRAM 执行等边角历史上有过 bug（libretro PR #289 已修），用较新的 libretro/gpsp 主线即可。

---

## 4. 内存分析（能否塞进 ~100MB）

**能，非常宽裕。** gpSP 的内存模型天生为极小 RAM 设计：

- **历史铁证**：gpSP 当年在 **PSP Phat（总共 32MB 系统 RAM）** 上就满速跑 GBA；RS-97 也只有 **32MB**。100MB 是它们的 3 倍。
- **ROM 按需分页**：ROM 切成 32KB 页（最多 1024 页 = 32MB），**LRU 换入换出**，不必把整颗 32MB ROM 常驻。典型 ROM 缓冲区仅 **16–32MB**（可配小）。
- GBA 内部内存很小：256KB EWRAM + 32KB IWRAM + 96KB VRAM + 1KB OAM + 1KB Palette ≈ **<400KB**。
- 翻译缓存（JIT code cache）通常 **几 MB ~ 十几 MB** 量级，可配置。
- 视频/音频/缩放缓冲：GBA 帧 240×160×2B≈75KB，缩放目标缓冲 <1MB，音频环形缓冲几十 KB。

**粗算峰值**：ROM 缓冲 16–24MB + JIT cache ~8–16MB + 内核/GBA RAM <1MB + I/O 缓冲 <3MB ≈ **30–45MB**，留给系统 50MB+。**内存不是瓶颈**（甚至可以把 ROM 缓冲开到 32MB 全常驻省掉分页开销）。

---

## 5. 编译 / 依赖

- **交叉编译**：标准 mipsel glibc 工具链即可，`-march=mips32r2`（保守、X2000 原生支持）静态链接。工程里已有 mipsel 静态编译 InfoNES/tinyalsa 的先例，工具链现成。
- **依赖**：
  - **gpSP/ReGBA 内核本身几乎零外部依赖**（自带 dynarec、渲染、音频混音逻辑）。
  - **前端 I/O**：
    - **picogpsp + libpicofe**：libpicofe 支持 **直接 framebuffer** 输出，最贴合 fb2 裸机；音频走 OSS/ALSA。**最省依赖**。
    - **ReGBA**：默认 **SDL1.2**。两条路：(a) 交叉编译一份 SDL1.2 的 `fbcon`(directfb/fbdev) + `dsp/alsa` 后端；(b) 直接把它的 SDL blit/audio 替换成**裸 fb2 写显存 + tinyalsa**（工程里已经在用 tinyalsa）。推荐 (b)，摆脱 SDL。
- **触摸/按键**：X2000 只有电容触摸 + 少量物理键。需要自写输入层：物理键映射到 GBA 的 A/B/L/R/Start/Select/方向，触摸区做屏幕虚拟按键（onscreen d-pad/按钮）。这部分是**纯新写的适配代码**，与 NES 移植同性质。

---

## 6. 移植路线与工作量预估

推荐 **ReGBA（独立 SDL 应用，功能全）** 或 **picogpsp（libpicofe，最贴 fb）** 二选一，套路一致：

1. **拉起内核**：用 `-march=mips32r2` 静态交叉编 gpSP/ReGBA MIPS dynarec，先跑 headless / 打印帧率，验证 **JIT mmap+cacheflush 在 4.4 内核 + 已 root 环境正常**。（1–2 天）
2. **显示层**：把内核输出的 240×160×16bpp 帧，缩放/整数放大写进 fb2（横屏 800×340；建议 ~510×340 适配或 480×320 整数 2× 居中）。复用工程里 fbtest 的 fb 通路。（2–4 天）
3. **音频层**：内核音频回调 → tinyalsa（工程已集成）→ ES8326。处理采样率转换（GBA 32.768kHz → 48kHz）与缓冲。（1–3 天）
4. **输入层**：物理键 + 触摸虚拟按键映射到 GBA 键位。（2–4 天）
5. **整合/调优**：frameskip、限速到 60fps、存档路径、ROM 加载 UI/C1Home 入口、少数重游戏调 frameskip。（3–5 天）

**总工作量估计：约 1.5–3 周（单人）**，其中 dynarec/CPU 仿真部分基本"白嫖"，绝大部分工时花在 fb/audio/input 适配和联调——**和把 InfoNES 落地到本机是同一类工作**。

**主要风险点（都不致命）**：
- JIT 可执行内存 + cache flush 在该 BSP 上的行为（4.4 已 root，风险低）。
- 缩放 blit 的 CPU 开销叠加到重游戏时挤占预算（可用 frameskip / 整数缩放缓解）。
- SDL 依赖处理（选 picofe/裸 fb 可绕开）。

---

## 7. 与 NES（InfoNES，已在做）的难度对比

| 维度 | NES (InfoNES) | GBA (gpSP) |
|---|---|---|
| CPU 仿真 | 6502 **解释**即可满速（NES CPU 1.79MHz，极轻） | ARM7 @16.78MHz，**必须 dynarec** 才满速 |
| 是否要 dynarec | ❌ 不需要 | ✅ 需要，**但 MIPS 后端现成** |
| 代码复杂度 | 小、单文件级、好懂 | 大、含 JIT，**但当黑盒用不必读懂** |
| 内存 | 极小（<1MB 级） | 中（30–45MB），仍宽裕 |
| I/O 适配（fb/audio/input） | 需要（同类工作） | 需要（**同类工作，可复用 NES 的 fb/tinyalsa/输入代码**） |
| 净增难度 | 基线 | **≈ 基线 + "让现成 MIPS dynarec 编过并跑起来" + 更重的缩放/音频调优** |

**结论**：GBA 比 NES 难，但**没有数量级差距**。难点从"写仿真"变成"把一个成熟的 mipsel dynarec 模拟器换 I/O 层"。因为 MIPS dynarec 后端已存在且 X2000 原生兼容 MIPS32r2，最可怕的部分（写编译器后端）被完全跳过。NES 的 fb/ALSA/输入代码可直接复用到 GBA，边际成本进一步降低。

---

## 8. 一句话总结

**在快易典 C1 Max（Ingenic X2000）上跑 GBA 完全可行——gpSP/ReGBA 的成熟 MIPS32r2 dynarec 已在同族更弱的 JZ4770 掌机上把绝大多数游戏跑到满速，X2000 单核更强、内存对 gpSP 而言绰绰有余，唯一实打实的工作量是给现成模拟器接上本机的 framebuffer / ALSA / 触摸输入，预计单人 1.5–3 周。**

---

### 参考来源

- gpSP (libretro, davidgfnet 维护)：<https://github.com/libretro/gpsp> · <https://github.com/davidgfnet/gpsp>
- gpSP Makefile（`CPU_ARCH := mips` + `HAVE_DYNAREC := 1`，gcw0/retrofw/rs90/psp1/ps2 目标）：<https://github.com/libretro/gpsp/blob/master/Makefile>
- picogpsp（libpicofe 独立前端，framebuffer/SDL，TrimUI 等）：<https://github.com/shauninman/picogpsp>
- ReGBA（RetroFW 端口，独立 SDL，gpSP 0.9 衍生）：<https://github.com/retrofw/regba> · `Makefile.gcw0`：<https://github.com/pingflood/regba/blob/master/Makefile.gcw0>
- ReGBA / gpSP 在 GCW Zero(JZ4770) 实测（多数满速，Golden Sun 2 最低 51fps，Payback ~14fps）：<https://gbatemp.net/threads/regba-v1-45-gameboy-advance-emulator-for-gcw-zero.357208/> · <https://forum.gamehacking.org/forum/important/hacking-scene-news/9322-regba-v1-45-gba-emulator-for-gcw-zero>
- gpSP MIPS dynarec 用 MIPS32r2 EXT/INS/ROTR（Dingux dynarec 讨论）：<https://pyra-handheld.com/boards/threads/gpsp-for-dingux-with-dynarec.48779/>
- gpSP ROM 按需分页 / 内存模型：<https://deepwiki.com/libretro/gpsp/2.2-running-games> · <https://www.gamebrew.org/wiki/GpSP>
- dynarec 从 VRAM 执行修复：<https://github.com/libretro/gpsp/pull/289>
- X2000 = XBurst II / MIPS32r5，双核，128-bit SIMD：<https://www.cnx-software.com/2020/07/12/ingenic-x2000-iot-application-processor-combines-32-bit-mips-xburst-2-cores-with-xburst-0-real-time-core/>
- MIPS32r5 向下兼容 MIPS32r2 / XBurst 兼容 MIPS32：<https://en.wikipedia.org/wiki/Ingenic_Semiconductor> · <https://lkml.iu.edu/hypermail/linux/kernel/2305.3/06442.html>
- GCW Zero(JZ4770) 规格：<https://en.wikipedia.org/wiki/GCW_Zero>
