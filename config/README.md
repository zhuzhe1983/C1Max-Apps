# 本地默认服务器

复制 `default-servers.example.json` 为 `default-servers.local.json`，填写 StreamPlayer 的 Emby/Jellyfin 与 CrossPoint 的 OPDS 服务器。空地址表示不提供默认值；匿名 OPDS 的 user/password 留空。密码是可选项，默认配置只预填界面，不会自动登录。

`*.local.json` 已被 Git 忽略，建议权限设为 `0600`。不要把真实地址、账号、密码或访问令牌写进示例；令牌不属于默认配置字段。

正常运行 `tools/deploy.py --serial … --start` 时，如果本地文件存在，会在应用包校验和激活后**单独**同步到设备 `/storage/apps/data/default-servers.json`（0600）。也可用 `--defaults /private/path/servers.json` 指定文件。默认配置不进入公开安装包、catalog 或 Git；没有本地文件时，不修改设备已有默认配置。

应用优先使用各自保存的设置；仅在没有可用设置时读取默认配置。现有 StreamPlayer 登录令牌与 CrossPoint 书库设置不会因部署默认值而被覆盖。登录/修改后仍写入各自的 data 目录。

`bash tests/run-defaults.sh` 在 Linux 下验证默认值读取、已有设置优先、令牌不导入和配置字段/URL 校验；需要 C++17、nlohmann JSON 和 Python 3。
