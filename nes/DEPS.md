# NES 依赖

InfoNES 提交固定在 `../dependencies.json`，由统一构建脚本获取到 `../.deps/InfoNES`。tinyalsa 共用 `../shared/tinyalsa`。

运行 `../tools/build.sh` 生成 `../.build/mips/c1max-nes`；统一部署见 [apps README](../README.md)。ROM 不进入版本库，将自己合法拥有的 `.nes` 文件放入设备 `/storage/apps/data/nes/roms/`，从 launcher 的 NES 入口选择。
