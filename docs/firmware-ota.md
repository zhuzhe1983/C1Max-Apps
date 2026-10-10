# C1 Max 固件通道与调试体系（2026-10-02 定稿）

设备：快易典 C1 Max 词典笔（Ingenic X2000 / mipsel / Linux 4.4 / 128MB RAM / eMMC 58GB）。
原厂已停止维护。本文档记录本机固件升级通道的逆向结论、自签与刷写工具链、
调试通道现状、以及所有已知陷阱。**改动任何一项前请先读「已知陷阱」。**

## 0. 从零开始（未破解设备如何拿到第一个 shell)

原厂倒闭后，唯一**已实证**的入门路径是伪造 adbAdmit 授权响应（2026-10-02 本机实操成功）:

**原理**：关于页隐藏手势会触发 `GET https://api.mpen.com.cn/v1/pens/{penId}?action=adbAdmit`,
响应 JSON 含 `"success":true` 即执行 `/usr/bin/enable_adb.sh true` 开 ADB。
真服务器对本机返回拒绝（`success:false`)，且设备**不校验 TLS 证书**（自签证书即可）。

**步骤**:
1. 在局域网任一机器上运行 `tools/mitm/adb_unlock_server.py`（自带自签证书，监听 443，
   需要 root/sudo;adbAdmit 一律批准，其余请求透明转发真实服务器，全量记日志——
   顺带能抓到设备真实 penId，查官方固件包时要用）;
2. 路由器上把 `api.mpen.com.cn` 指到这台机器（OpenWrt/dnsmasq:
   `address=/api.mpen.com.cn/<IP>`;LuCI: 网络 → 主机名映射）;
3. 设备连 Wi-Fi → 设置 → 关于 → **连点「系统版本」那一行 ≥10 次（任意 5 秒窗口内）**;
4. toast 判读：`-D` = 请求在途（等几秒再点一轮）;`-N` = 网络失败（检查 DNS 劫持）;
   「调试模式已经开启」= 批准已生效；响应到达即执行，无需再点；
5. 拔插一次 USB → `adb devices` 应出现（若只有 MTP，进设置把 USB 切成 ADB 再切回）;
6. 用完撤掉路由器劫持记录，停掉服务器。

**路径 B（MTP 刷包，研究中，暂不可用）**:`/storage/mtp/update_app.tar.gz` 通道校验
`storage/check.txt`(=「关于」页系统版本原文 + 逗号，如 `V1.54_MP-D350_20260119.114000,`),
理论上通过后以 root 解包到 `/`。但 2026-10-04 在当前固件上实测：check.txt 被正常读取，
载荷却未落地（新版固件的 checkAppValidate 疑似增加指纹/HMAC 校验，待继续逆向）。
**暂勿依赖此路径。**

**兜底**：UART 115200 8N1（主板测试点）/ USB boot(BOOT_SEL 测试点），见 §7「其他」。

拿到 adb 之后：按 README 构建部署应用；再按 §5/§6 做 OTA 自免疫（换 key + 隔离），
防止原厂通道把我们的工作冲掉。

## 1. 分区与刷机链路

```
p1 9M  p2 16M  p3 16M(nv 标志区)  p4 4M  p5 64M
p6 200M=/usr/resource  p7 100M=/usr/data  p8 500M=rootfs(/)  p9 57.3G=/storage
```

官方升级链路（全部实证）：

1. `mp_s300` 的 `checkupdate`（开机 500ms 定时器）发现 `/storage/mtp/update.zip`
   （或 OTA 下载完成、或 `/storage/mtp/update_app.tar.gz`）→ `property_set("init.svc.updater","OK")`；
2. init 触发 `preupdate` = `/etc/init.d/S40copy_ota_res`：`rm -rf /usr/data/ota_res`，
   从 `/etc/ota_res` 重新拷贝 recovery/脚本/key，并清零 `/dev/mmcblk0p3` 前 512 字节（nv）；
3. `preupdate` 停止 → init 启动 `updateservice` = `/usr/sbin/recovery`：
   验签 → `unzip /storage/update/update.zip -d /storage/update` →
   把 `update/update000/*` 拍平 → 按 update.xml 逐 image 刷写 → 写 nv[16]=0xa5a5a5a5、
   `/usr/data/UPDATE_RET=0` → 重启。

OTA 网络侧：开机首次联网自动 `GET https://api.mpen.com.cn/v1/pens/{penId}?&action=upgradeRom&version={displayId}`，
失败 60s 重试；服务器可下发 `isForce=1` 强制包，设备**零提示静默下载并自动重启刷机**
（2026-10-02 的事故即此）。penId 形如 `xxxxxxxx--xxxxxxxx-xxxxxxxx-xxxxxxxx`
（由 `/sys/class/net/wlan0/cid` 拼接，可在设备上读取；或用 §0 的 MITM 服务器从请求路径抓），
displayId 在 `/etc/system.ver`
（本机 `V1.54_MP-D350_20260119.114000`，「关于」页显示为 MP-C1 是替换后的字样）。

## 2. 验签与自签（tools/ota/）

验签为 AOSP mincrypt 整包签名：**RSA-2048 + SHA-1 + PKCS#1v1.5，e=65537**。

- 签名位置：zip 末尾 comment 区**最后 256 字节**，之后再追加 6 字节 footer
  （`pack('<H',total) + b'\xff\xff' + pack('<H',total)`，total = len(comment)+6）；
- SHA-1 覆盖 `zip[0 : eocd_offset+20]`（不含 EOCD 的 comment-length 两字节与 comment 本身）；
- comment 中不得出现 `PK\x05\x06`；EOCD 的 comment-length 字段**保持 0 不更新**（与 AOSP signapk -w 一致）；
- key 文件：`/usr/data/ota_res/key/key.pub`（preupdate 时从 `/etc/ota_res/key/key.pub` 刷新），
  v2 文本格式 `{len=64, n0inv=-n0⁻¹ mod 2³², modulus[64] LE32, rr=(2²⁰⁴⁸)² mod n}`。

工具：

- `tools/ota/mkkey.py <pub.pem> <out.v2.pub>` —— PEM 公钥转 v2 格式；
- `tools/ota/c1sign.py` —— 签名/验签复刻（`sign_zip()` / `verify_zip()`，与设备逐字节一致）；
- `tools/ota/c1key.v2.pub` —— 当前设备使用的公钥（**私钥 `c1key.pem` 不入库**，
  仅存于本机与编译机安全位置；丢失=永远失去签名权，需重刷 key）。

**设备现状：`/etc/ota_res/key/key.pub` 已替换为自生成公钥**（原厂备份在同目录
`key.pub.vendorbak`）。此后原厂 OTA 包验签必然失败；只有用 `c1key.pem` 签名的包能刷入。

## 3. update.zip 包结构（fullpkg 实证可用）

```
update.zip
└── update/
    ├── update000/{update.xml, device.xml, global.xml, sha1Tab}   ← 被 cp 拍平
    └── update001/<载荷文件>                                       ← pkgidx=1
```

- 三个 XML **必须带 `<?xml version="1.0" encoding="UTF-8"?>` 声明**（老 mxml 没有声明找不到根元素）；
- `devtype` 一律填 **`mmc`**（存储控制器类型，不是机型；nand/nor 会走 MTD 路径）；
- device.xml：9 个分区 item，offset/size 为真实字节值（本机实测见 `docs/` 同日的记录或
  `/sys/block/mmcblk0/mmcblk0pN/{start,size}`×512）；image 的 offset/size 必须完整落在某分区内；
- fullpkg = `dd if=<载荷> of=/dev/mmcblk0pN bs=… conv=fsync`（seek=0，写载荷实际大小，
  **无回读校验、不查 dd 退出码**——刷完必须自己回读验证）；
- `<bootmode><type>0</type></bootmode>`（与 nv[4]=0 一致，单次刷完不中间重启）；
- 载荷文件名与 `<name>` 一字不差（如 `rootfs.ext4`）；`updatemode type=512` 单文件不分片。

## 4. 刷写执行：tools/ota/flash-run3.sh

在 tmpfs chroot 中运行 recovery 完成刷写，要点（全部用事故换来）：

- **绝不对任何可能含挂载点的目录 `rm -rf`**（2026-10-02 事故：`rm -rf /tmp/f` 穿透
  bind mount 删除真实 /dev、/usr/data/ota_res、/storage/apps）；
- 用 `mktemp -d` 建独立目录；supervisor 与 recovery 同驻 chroot 前台串联：
  recovery 退出 → `sync` → 精确字节回读校验（`head -c $SZ`）→ 写 `/storage/flash.done` → 才 umount；
- `setsid /bin/busybox chroot ... &` 脱离会话（注意：**必须写 `/bin/busybox chroot`**，
  setsid 无法 exec busybox 的 chroot applet，无独立可执行文件）；
- 刷前清空 `/storage/update/` 里除 update.zip 外的一切（否则 unzip 交互式询问 replace，
  stdin 是 /dev/null 必败）；
- applet 清单必须含 `expr head`（update_stage.sh 依赖）；
- 库清单：ld-linux/libc/libm/libdl/libpthread/libgcc_s/libstdc++/libcjson/libresolv（`cp -L` 解 symlink）；
- 成功标志以 `/storage/flash.done` 内容 `exit=0 verify=VERIFY_PASS` 为准（不看文件存在与否）。

## 5. OTA 隔离（防官方通道复发）

`/etc/ota-quarantine.sh`（仓库 `tools/ota/ota-quarantine.sh`）作为 init 服务
`otaquarantine` 在 `on userboot`（紧随 `start smartUI`）执行，把
`/storage/mtp/update.zip`、`/storage/update/update.zip`、`/storage/mtp/update_app.tar.gz`
挪到 `/storage/apps/data/ota-quarantine/`。与 key 替换构成双保险：
官方包既到不了刷写器，到了也过不了验签。

想跟随原厂升级时：从隔离区（或按 penId 查 `upgradeRom` 拿 downloadUrl）取包 →
解开 → 把新 rootfs 镜像按本文件 §6 的方式注入 → `c1sign.py` 自签 →
放 `/storage/update/update.zip` → 跑 `flash-run3.sh`。

### tools/ota/inject-and-sign.sh 一键注入

上面这条手工链已固化成脚本（目标环境：编译机 Debian，需 zip/unzip/e2fsck/
mount/python3/openssl + 免密 sudo；**不支持 macOS**）：

```
inject-and-sign.sh <in.zip> <out.zip> <c1key.pem> <c1key.v2.pub>
```

- 输入为原厂结构的 update.zip（`update/update000/` 元数据 + `update/update001/` 载荷）；
- 自动在 `update001/` 里识别 ext 系列文件系统且 ≥400MiB 的 rootfs 载荷
  （`blkid -p`，`file` 兜底；零个或多个候选都直接报错，不猜）；
- `e2fsck -fy` 后 loop mount，按 §6 做全部注入：init.rc 五处
  （三条 setprop、c1desktop 块、hotkey 块、otaquarantine、c1telnet）、
  `/etc/ota-quarantine.sh`（755 root:root）、key.pub 替换（先备 `key.pub.vendorbak`，
  已存在不覆盖）、usbconfig.sh 的 `let overtime=` 笔误修复——**全部幂等**，
  已注入的包重跑全 SKIP；
- `zip -1` 重组后内嵌 python 复用 `c1sign.py` 的 `sign_zip`/`verify_zip`：
  从 c1key.pem 用 `openssl rsa -text` 提取 modulus/privateExponent 签名，
  并交叉核对 PEM 与 v2 公钥模数一致，验签 PASS 才产出；
- 工作目录一律 `mktemp -d`，trap 先 umount 再清理，挂载点目录只用 `rmdir`，
  umount 失败宁可留下现场也绝不 `rm -rf` 含挂载点的目录；
- 产物按提示 `adb push` 到 `/storage/update/update.zip` 后跑 `flash-run3.sh` 即可。

## 6. 注入版 rootfs 的标准内容（当前 p8 已包含）

- init.rc：
  - `on init` 段：`setprop user.usb.config adb`、`setprop service.adb.tcp.port 5555`（本机 adbd
    不支持 tcp，见 §7）、`setprop sys.backlight.lock 1`（防真休眠）；
  - `on property:init.svc.smartUI=running` → `start c1desktop`（+ 冗余 setprop）；
  - `service c1desktop /storage/apps/current/launcher/desktop-service.sh`（disabled, oneshot）；
  - `service c1apps-hotkey /storage/apps/current/shared/c1max-hotkey`（class main, disabled，
    由 `tools/hotkey_service.py install` 安装，带备份/校验）；
  - `service otaquarantine /etc/ota-quarantine.sh`（disabled, oneshot）；
  - `service c1telnet /usr/sbin/telnetd -p 2323 -l /bin/sh`（on boot 随 network 启动）；
- `/etc/ota_res/key/key.pub` = 自有公钥（+ `key.pub.vendorbak` 原厂备份）；
- `/usr/bin/usbconfig.sh`：修复君正原厂 `let overtime=overtime-1` → `overtimer`
  （变量名笔误导致重试循环永不超时，UDC 绑定慢时 usbrecfg 永久卡死）；
- 设备侧 `/storage/apps/data/launcher/adb.onboot` 标志 → desktop-service.sh 在 smartUI
  稳定后执行 `enable_adb.sh true`（兜底），并按用户偏好重放息屏策略（见 §7「息屏/睡眠策略」）。

## 7. 调试通道与已知陷阱（每条都踩过）

**ADB / USB**
- `enable_adb.sh true` 只写**非持久**属性 `user.usb.config=adb`；重启即失效，故依赖 §6 的埋点；
- **绝不要重启 adbd**：`ctl.restart adbd`、`adb tcpip 5555`、`setprop ctl.stop adbd` 都会让
  adbd 永久死亡（init 不会拉起，gadget 消失）。恢复：设置里 USB 切 MTP 再切 ADB，或重启；
- 本机 adbd（116KB 老 AOSP 构建）**不支持 `service.adb.tcp.port`**，无线 adb 协议不可达；
  无线通道 = `nc <设备IP> 2323`（telnetd，root shell，无认证——限家庭内网调试用）；
- gadget 配置读取的是 `user.usb.config`；`sys.usb.state=adb` 触发 `start adbd; start mtp`
  （adb 模式下是 adb+mtp 复合）；
- **smartUI/mp_s300 启停会重新配置 USB gadget，adb 会话必然断开**——每次切原厂/自定义
  桌面 adb 都会掉一次，属预期；此时 Wi-Fi telnet（2323）仍然可用；
- 设备真休眠会令 USB gadget 下电（表现为 adb 掉线）——开机默认 `sys.backlight.lock=1`
  已抑制；若用户在设置里自选熄屏超时，息屏/休眠按原厂计时发生（见下「息屏/睡眠策略」）。

**息屏/睡眠策略（持久化，2026-10-02 起）**
- 归属：息屏策略是**用户偏好**，设置应用「自动熄屏」拥有并修改它；`adb.onboot`
  只是调试便利总开关（开机 ADB），不再强制屏幕常亮；
- 链路：init.rc `on init` 先给 `sys.backlight.lock=1`（早期默认，防真休眠）→
  smartUI 稳定后 desktop-service.sh 读 `/storage/apps/data/settings/screenoff`
  逐行重放 `lock`/`timer`（缺行跳过该项），最后 `setprop sys.backlight.timer.reset 1`；
  文件不存在 = 调试默认 `lock=1`（屏幕常亮）；
- 文件格式（设置应用写入）：`lock=<0|1>` 与 `timer=<ms>` 两行；「永不」=
  `lock=1` + `timer=0`，超时档 = `lock=0` + 对应毫秒；
- 设置应用回显以该文件为准，文件不存在才按当前属性推断；
- 三个 `sys.backlight.*` 属性本身仍是非持久的，重启即丢，持久层只有这个文件。

**原厂设置库 settings.db（息屏踩踏源）**
- `/usr/data/database/settings.db` 是原厂设置的持久层：**不是 sqlite**——记录以 `0x0a`
  分隔，每条 = NUL 填充的 ASCII 键 + NUL 填充的 ASCII 值（全机约 1.1KB，十几条）;
- **mp_s300 每次启动都会重设** `sys.backlight.lock=0`、`sys.backlight.timer=<库值>×1000`
  ——包括作为我们 launcher 子进程启动（词典入口）时，会盖掉开机重放的策略；
- 库中相关键：`sys.backlight.timer=60`（秒，出厂值）、`sleep_timeout=180`、
  `poweroff_time=1800`、`sys.timing.shutdown=15`、`lcd_bright*`、`volume.persent` 等；
- 调试期已把前三个改为 `86400`（24 小时=实际不息屏/不休眠/不自动关机）：
  原地把 ASCII 值覆盖进 NUL 填充区、保持记录长度不变即可，mp_s300 下次启动生效；
- 本机「息屏」= 真休眠：USB gadget 下电、Wi-Fi 断，adb/telnet 全灭，按电源键唤醒。

**原厂隐藏机制**
- 「关于」页连点「系统版本」≥10 次/5 秒 → 弹 toast 并向
  `api.mpen.com.cn/v1/pens/{penId}?action=adbAdmit` 请求授权；真服务器对本机返回
  `{"data":{"success":false}}`（拒批）。toast：`-D`=触发时授权值 0（请求在途/被拒），
  `-N`=上次请求失败，「调试模式已经开启」=已批准并执行 `enable_adb.sh true`；
- 「型号」行 500ms 内连点 5 次 = 重新注册请求；「蓝牙」行 5 次/5 秒 = 授权码页；
- 连点触发的调试菜单项可见性由点击计数驱动，窗口过期即消失，不是持久开关。

**MTP / update_app.tar.gz 通道**
- MTP 根 = `/storage/mtp`（只见 diy/record/music/Pictures 等，碰不到系统分区）;
- `checkupdate`(mp_s300 启动时跑）会处理 `/storage/mtp/update_app.tar.gz`:
  先单独解出 `storage/check.txt` 校验（内容应为 `<displayId>,<publisher>`，
  displayId = `/etc/system.ver` 原文，本机 publisher 为空，即
  `V1.54_MP-D350_20260119.114000,`)，通过后以 root 执行
  `mount -o remount,rw rootfs /; tar -zxf ... -C /`（该命令字符串在二进制中）;
- **2026-10-04 实测：check.txt 被正常读取校验，但载荷未落地**（探针文件未出现，
  设备未重启）——当前固件的 `checkAppValidate` 很可能还有指纹/HMAC 校验
  (`PenHasher::getHmacBinaryStringFingerPrint` 为外部导入），**此通道暂不可用**,
  从零破解请走 §0 的 MITM 路径。

**launcher 桌面链**
- desktop-service.sh 用「首帧后写 `$C1L_HEARTBEAT` 文件」判定 launcher 真的拿到了
  framebuffer，超时回退原厂 UI（回退日志在 `/storage/apps/data/launcher/desktop-boot.log`）；
- 心跳逻辑必须在 launcher 源码里（曾只存在于未提交构建，重建后全机回退）；
- 我们 launcher 前台时 `run.sh` 会 `ctl.stop smartUI`——此时 mp_s300 不在，
  MITM/抓包看不到它的 API 请求属正常，不是死机。

**其他**
- 原厂 OTA 重刷会完整重写 rootfs（init.rc 改动全灭），但不会动 /storage——
  现以 key 替换 + 隔离彻底关门（§5）；
- `/usr/data/ota_res` 每次 preupdate 被 `/etc/ota_res` 全量刷新，改 key 要改 `/etc` 那份；
- nv(p3) 布局：`[4]` bootmode、`[8]` 已完成镜像序号、`[12]`0x5a5a5a5a 刷写中、
  `[16]`0xa5a5a5a5 完成；全 0 = 正常启动；
- 刷写器失败行为：验签失败→挂起（断电重启即恢复）；其余失败→UPDATE_RET=101 只重启；
- 救援兜底：UART 115200 8N1 3.3V（主板测试点）；USB boot(VID a108:eaef）需 BOOT_SEL
  测试点；x86 Linux + ballaswag/ingenic-usbboot 可按扇区 dump/写回 eMMC
  （先做全量备份；macOS 无法透传 USB 给 Docker）。

## 8. 验证记录（2026-10-02）

1. 彩排：自签小包（devtype 故意不匹配）通过真机验签，在写盘前中止 → 证明签名布局正确；
2. 实刷：201MB fullpkg 包（p8 rootfs 镜像，含本文档全部注入）→ 验签 → 解包 →
   dd 500MB 写 p8 → `UPDATE_SUCCESS`、`UPDATE_RET=0` → 回读 sha256 与载荷逐字节一致 →
   刷后启动 adb 自动、launcher 心跳前台、key/服务块全在；
3. 第三方 review(codex CLI 三轮）：抓出 1 个致命问题（刷写脚本生命周期）、
   3 个确定性问题（expr applet、精确字节校验、sync 时机），均已修复；
   EOCD comment-length 等异议经真机证据排除。
