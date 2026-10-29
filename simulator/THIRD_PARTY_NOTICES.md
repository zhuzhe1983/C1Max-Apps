# 第三方来源与许可证

模拟器用户态工具沿用 C1 Slim QEMU 模拟器的 GPL-3.0-or-later 构建和音频基础。本项目目录与运行数据独立。`guest/driver/c1maxsim.c` 为 GPL-2.0-only 模块。

| 组件 | 来源 | 许可证与声明 |
|---|---|---|
| Linux 6.6.60 | https://cdn.kernel.org/pub/linux/kernel/v6.x/ | GPL-2.0-only，另有内核各文件声明 |
| BusyBox 1.37.0 | https://busybox.net/downloads/ | GPL-2.0-only |
| FFmpeg 6.1.5 | https://ffmpeg.org/releases/ | 本构建 LGPL-3.0-or-later；启用 OpenSSL/version3，不启用 GPL/nonfree 编码器 |
| Debian Bookworm MIPS OpenSSL | Debian `libssl-dev:mipsel` | 以实际包内 copyright 为准，构建保存版本和声明 |
| CA bundle | Debian ca-certificates | 保留包内 copyright |
| C1Max-Apps 基础源码和官方包 | `upstream.json`、`catalog.json` | 上游未声明统一仓库许可证；保留每个资源包与源目录中的声明 |
| Noto Sans SC | 上游 shared/fonts | SIL OFL，字体和 OFL 原文进入客体 |
| stb_truetype_htcw | 固定 LVGL 提交 | 保留上游启动器已有的 stb 许可证原文 |
| nlohmann/json | Debian `nlohmann-json3-dev` | MIT；保留 Debian 包内 copyright，用于最新基础音频服务的投屏客户端依赖 |

下载的源码在 `.cache/`，依赖声明在 `build/licenses/`，应用自己的许可证在 `build/payload/<id>/licenses/`（以包布局为准）；打包时保留资源和许可证。模拟器的 GPL 许可证不能作为给所有上游应用重新授权的依据。

本地构建不发布二进制。若将来分发，须连同具体构建的适用许可证和对应源码、构建配方提供，不能只分发镜像而省略其依赖义务。
