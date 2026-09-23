# C1Max 计算器

从 `~/Workspace/m5stack/CardputerZero/Calculator` 移植的原生 Linux 计算器，
使用 LVGL 9.5 和 C1Max 共用的 framebuffer / 触摸驱动。
800 × 340 横屏左侧显示表达式、结果和错误，右侧保留可直接点击的计算按钮。
输入使用词典真实物理键盘，不创建屏幕虚拟键盘。

## 操作

- 四则运算、小数、`+/-` 切换当前操作数正负、括号、乘方。
- `DEL` 退格，`AC` 清空，`=` 计算；先乘除再加减，乘方从右向左结合。
- 结果后按运算符继续计算，输入数字开始新计算。
- 除零、括号未闭合、结果溢出等会显示错误，保留表达式以便退格修改。
- 物理“返回”键或右上角“返回”按钮返回 launcher；电源键由共享输入层处理。
  `SIGTERM` / `SIGINT` 也会退出并释放 framebuffer。

按实际键帽使用 `Shift` 组合输入数字和运算符；共享键盘驱动负责转换为 ASCII，
计算器直接接受 `0–9`、`.+-*/^()`、`Enter` 和 `Backspace`，不再自行解释 Shift。

- `Shift+Q W E R T Y U I O P` 输入 `1 2 3 4 5 6 7 8 9 0`。
- `Shift+J` 乘、`Shift+C` 除、`Shift+X` 减、`Shift+Z` 小数点。
- `Shift+K/L` 输入左右括号；键帽没有加号，使用应用快捷键 `A` 输入加号。
- “确认”键计算，“退格”键删除，“返回”键退出（底层 Linux `KEY_DELETE` 转为 `screen::KEY_EXIT`）。

另外提供以下应用字母快捷方式，便于无需 Shift 连续计算；这些是应用定义的快捷键，
不是硬件键帽映射：

| 物理键 | 功能 |
| --- | --- |
| `Q W E R T Y U I O P` | `1 2 3 4 5 6 7 8 9 0` |
| `A S D F` | `+ - * /` |
| `Z` / `X` | 小数点 / 当前操作数正负 |
| `J K` / `L` | 左右括号 / 乘方 |
| `Enter` / `Backspace` | 计算 / 退格 |
| `C` 或空格 | 清空 |
| 返回键 | 退出 |

共享驱动的 `screen::take_key()` 统一传递按下及重复事件，计算器不再挂接第二套
LVGL keypad 处理，避免一次按键重复输入。直接 ASCII 数字和运算符仍可用于兼容输入。

应用不写任何文件，也不需要网络。原桌面停止/恢复由上层 launcher 的会话包装器负责；
计算器本身不终止其他进程。

## 目录与来源

```text
calculator/
├── src/engine.{hpp,cpp}  表达式解析及结果格式化
├── src/model.{hpp,cpp}   与 UI 解耦的编辑状态
├── src/main.cpp         C1Max 触摸界面、退出与诊断入口
└── tests/               主机端计算和按键状态测试
```

解析器移植自 `CardputerZero/Calculator/main/src/calculator_engine.cpp`，
保留其递归下降语法和运算优先级，未修改来源工程。
没有沿用 `cp0-Calculator0` 的科学计算器 UI、CardputerZero 键盘映射或 AArch64 包装。
本次补充科学记数法结果的继续运算、独立的除零/范围错误、表达式长度与递归深度限制。
结果按 12 位有效数字显示，小数极小值不会因为固定小数格式被显示成零。
使用 `double` 计算，不提供任意精度；表达式最多 192 个 ASCII 字符。

## 构建集成

在 `apps/CMakeLists.txt` 中添加以下目标，使用项目统一 MIPS 工具链：

```cmake
add_executable(c1max-calculator
    calculator/src/main.cpp
    calculator/src/engine.cpp
    calculator/src/model.cpp
    shared/display.cpp)
target_include_directories(c1max-calculator PRIVATE shared)
target_link_libraries(c1max-calculator lvgl m)
```

运行时字体为 `/storage/apps/current/shared/NotoSansSC-Regular.ttf`。
`C1_APPS_ROOT` 可覆盖 `/storage/apps/current`，LVGL 文件路径使用 `A:` 前缀。
字体缺失时输出诊断并降级为英文内置字体，避免出现无字按钮。

## 验证

主机端无需 LVGL 即可运行计算逻辑和编辑状态测试：

```sh
apps/calculator/tests/run.sh
```

交叉编译后可在设备上验证解析器，不会访问显示设备：

```sh
c1max-calculator --evaluate '3*-2+(1/8)'
# -5.875
c1max-calculator --evaluate '1/0'
# 标准错误输出 division_by_zero，退出码 2
```

通过 launcher 的会话包装器停止原桌面后可运行有限时 UI 检查：

```sh
c1max-calculator --smoke-ms 3000
```

真实触摸回归：`1` → `+` → `2` → `=` 显示 `3`，再按 `*` → `2` → `=`
显示 `6`；`AC` → `1` → `/` → `0` → `=` 显示除零错误；`DEL` → `2` → `=`
显示 `0.5`；点击“退出”返回 launcher。

真实物理键回归：通过键帽上的 Shift 组合输入 `1+2`，`Enter` 显示 `3`，再输入 `*2`
得到 `6`。应用快捷键回归：`Q` → `A` → `W` → `Enter` 显示 `3`；`D` → `W` → `Enter`
显示 `6`。`C` → `Q` → `F` → `P` → `Enter` 显示除零错误；`Backspace` → `W`
→ `Enter` 显示 `0.5`。“返回”键退出；驱动交付大写字母时，应用快捷键忽略大小写。

用于注入测试的横屏按键中心：`1`=(392,250)、`2`=(505,250)、`+`=(731,250)、
`=`=(675,306)、`AC`=(392,82)、`0`=(392,306)、`/`=(731,82)、`DEL`=(618,82)、
退出=(736,24)。触摸驱动坐标需按共享显示层的旋转映射转换。
