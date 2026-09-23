# Processing 2D：C1Max 轻量兼容版

这是 QuickJS 驱动的 **Processing 风格 JavaScript 2D 子集**，不是官方 Processing Java 运行时，也不是完整 p5.js。目标是让 128 MB 的 MIPS Linux 词典可以直接浏览、修改和运行绘图程序。

## 为什么用兼容版

设备系统报告总内存 103112 KiB，停止原装界面后通常约有 65 MB 可用。评估时官方 Processing 4.5.6 发布 Linux x64 / aarch64 包，没有 MIPS32 包；设备也没有 Java、X11/Wayland 或浏览器。官方开发运行脚本的 128–512 MB Java 堆配置进一步超出了这里的余量，但它并不是所有 Processing 草图的最低内存要求。当前无法直接安装官方版本，因此选择小型兼容实现。

四个示例在真机各运行 180 帧，包括 Koch 第 5 级，测试进程峰值约 2.5 MB。此结果是本机移植示例的实测，不能用来推断任意 Processing 程序都能运行。完整 GUI 在浏览示例、编辑和保存重开后的实测 RSS 峰值约 3.3 MiB；framebuffer 使用内核映射，不能把进程 RSS 当作整机全部内存占用。见仓库 `VALIDATION.md`。

## 示例与编辑

- Q：树形分形，拖动画布改变分叉角度。
- W：Koch 曲线，逐级展开，点击重新开始。
- E：群聚模拟，有限数量的个体执行分离、对齐、凝聚；点击加入个体。
- R：粒子系统，点击改变发射位置。
- T：我的程序；H：帮助。

前四项改编自 Daniel Shiffman 的官方 Processing 示例。原始 PDE、上游 README 的 public-domain 声明、固定提交与文件清单位于 [examples/upstream](examples/upstream/) 和 [SOURCES.json](examples/SOURCES.json)。为了适应屏幕和内存，调整了画布尺寸、对象数量和交互。原始文件保留供比较；运行的是对应 `.js` 改编版本。

运行时 P / 空格暂停或继续、R 重置、E 编辑、中间返回键回示例。点击标题也可暂停。编辑时仅使用物理键盘：右上退格删除、右下回车换行、双击 Shift 切换大小写，Shift + 字母输入键帽上的数字符号。

相机键作为一次性前缀：按一次后 WASD 移动光标，Q/E 到文件开头/末尾，T 输入两个空格；连续两次后按以下字母输入标点；第三次或返回键取消前缀。

| 字母 | Q | W | E | R | T | Y | U | I | O | P | A | S | D | F | G | H |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 符号 | `[` | `]` | `{` | `}` | `<` | `>` | `=` | `+` | `_` | `\` | `"` | `'` | 反引号 | `!` | 竖线 | `^` |

“保存并运行”执行当前程序；返回键保存并回示例，电源键保存并回 launcher。编辑示例时保存为自己的副本，不覆盖内置原稿。草稿位置为 `/storage/apps/data/processing/my-sketch.js`，最多 16384 个字符，UTF-8 文件读取上限为 64 KiB。语法、运行超时和内存错误会显示错误页，可以返回编辑。

## 支持的 API

画布固定 **400×145**，显示放大到 800×290，顶部保留控制栏。使用 JavaScript 的 `function setup()` / `function draw()`；`mousePressed()` 和 `keyPressed()` 可选。运行栏保留的 P/R/E/空格不会传给程序。

- 状态：`width`, `height`, `frameCount`, `mouseX`, `mouseY`, `mouseIsPressed`, `key`。
- 绘制：`background`, `color`, `fill`, `stroke`, `noFill`, `noStroke`, `line`, `rect`, `ellipse`, `triangle`。坐标为画布坐标，线宽固定 1 像素，`ellipse` 默认中心定位，`rect` 默认左上定位。
- 变换：`translate`, `rotate`, `pushMatrix`, `popMatrix`；每帧重置矩阵。填充椭圆目前只平移，轮廓支持旋转，因此请不要旋转非圆形的填充椭圆。
- 多边形：`beginShape`, `vertex`, `endShape`，仅三角扇拆分；不支持凹多边形、曲线或完整 Processing shape 模式。
- 数学：`sin`, `cos`, `sqrt`, `abs`, `pow`, `floor`, `ceil`, `min`, `max`, `atan2`, `random`, `map`, `constrain`, `radians`, `int`；`PI`, `TWO_PI`, `HALF_PI`。
- 向量：`PVector` 的 `copy/add/sub/mult/div/mag/normalize/limit/rotate/heading2D`，以及静态 `sub/dist`。
- `size` / `createCanvas` 只接受 400×145；`noLoop` / `loop` 控制后续 draw 调用。目标帧率 15 fps，复杂程序可能更慢。

不支持 Java/PDE 语法、P3D、WebGL、DOM、图像/字体加载、网络、文件访问、音频或外部 Processing/p5.js 库。`api.js` 是支持范围的完整定义。

QuickJS 堆限制 8 MiB、栈限制 256 KiB；载入和 setup 分别最多 500 ms，单帧或输入回调最多 150 ms；形状最多 3000 顶点、变换栈最多 64 层。越界绘制裁剪到画布。这些约束用于防止普通错误卡死界面，不能把同进程 JavaScript 引擎当作执行恶意程序的完整安全沙箱。

## 构建和验证

随仓库 `./tools/build.sh` 构建。QuickJS 固定版本与 SHA-256 位于 `archives.json`，MIT 许可证随设备包分发。设备测试：

```sh
adb push .build/mips/c1max-sketch-test /tmp/
adb push processing /tmp/c1max-processing-source
adb shell '/tmp/c1max-sketch-test /tmp/c1max-processing-source; echo TEST_EXIT=$?'
```

测试包括四个示例、无限循环中断、堆上限、错误后恢复、离屏线条裁剪、颜色混合及触摸按下边沿。

参考：[官方下载](https://processing.org/download/)、[Processing 构建说明](https://github.com/processing/processing4/blob/main/build/README.md)、[QuickJS](https://bellard.org/quickjs/)、[官方示例仓库](https://github.com/processing/processing-examples)。
