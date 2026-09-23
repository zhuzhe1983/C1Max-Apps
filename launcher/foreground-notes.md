# 自定义应用的前台切换与恢复

`run.sh` 是自定义桌面的监督器。所有自定义应用通过它进入，不能直接运行
`c1max-launcher` 后再手工杀原装桌面。

## 节省内存的范围

进入前调用 `setprop ctl.stop smartUI`，确认服务为 `stopped`，且 `mp_s300`
进程已经退出，再启动自定义桌面。原装桌面、查词和学习页面主要处在同一个
`mp_s300` 进程中；关闭这个进程也关闭这些界面。此前设备采样的 RSS 约
40 MB；实际增加的可用内存应通过切换前后的 `/proc/meminfo` 比较，不能把
RSS 全部当成一定回收的内存。这里使用进程退出，不使用保留内存的 SIGSTOP。

不按进程名称批量杀其他程序，不停止 Wi-Fi、ADB、PowerManager、媒体或系统
服务。词典的 MPlayer 使用 `-ao media`，需要保留 `mediaserver`；停止它会
破坏播放器的音频通路。尚未确认某个厂商辅助服务归属于当前 UI 会话前，不
自动停止它。因此这不是“关闭所有厂商服务”模式。

原装桌面重新启动后回到其启动页面；原先打开的词典页面不承诺保留。

## 物理音量键

`shared/c1max-volume` 随本次前台会话运行，同时监听 matrix keyboard 的
`event0` 和 GPIO keys 的 `event1`。它不独占输入设备，只处理
`KEY_VOLUMEDOWN/KEY_VOLUMEUP` 的按下与长按重复，150 ms 节流；每次读取
card0 的双声道 `softvolume` 当前值，增减 13，并限制在控件声明的范围内。
守护器不操作 DAC、功放开关或 mediaserver，所有自定义应用共用这一份处理。

监督器会检查音量守护器的 PID 和起始时间，异常退出时恢复原厂前台；返回
原厂前先停止守护器，再恢复进入前的 ALSA 快照。因此本次自定义会话中的
音量调整不会改写原厂 SettingsDB 的 `volume.persent`；返回原厂后恢复进入
前的音量，与原厂设置保持一致。守护器设有父进程死亡信号，监督器意外消失
后会退出，避免后台继续修改原厂音量。

## 恢复行为

- 正常 EXIT、launcher 异常退出：监督器等待 launcher 结束，恢复进入前保存
  的 ALSA mixer 状态；原装 smartUI 进入前在运行时，才重新启动它，并验证
  服务状态和进程存在。单个子应用正常退出则先返回自定义桌面。
- 对监督器发送 TERM/INT/HUP：先让 launcher 退出。launcher 会终止自己启动的
  应用进程组；监督器最多等待 6 秒，再只向身份仍匹配的 launcher 发 KILL。
- 进入前 smartUI 已停止：结束时保持停止，不替用户启动一个原本关闭的桌面。
- `flock` 防止并发进入；`run.lock` 记录监督器和 launcher 的 PID、boot ID、
  `/proc/PID/stat` 起始时间。进程身份必须全部匹配才允许发送信号，避免 PID
  被复用后误杀其他进程。持久锁文件 `foreground.lock` 不应删除或替换。
- mixer 保存失败会记录日志，仍可进入；恢复失败会记录日志，继续尝试恢复
  原装桌面。启动原装桌面失败时保留恢复记录，以便下一次进入时继续恢复。
- 监督器本身遭 SIGKILL、断电或内核崩溃时，shell 的 trap 无法执行。下次
  启动会检查遗留记录；发现仍存活的 launcher 时拒绝另开一个前台；确认旧
  launcher 已退出后可延续本次启动周期内的恢复义务。重启后不复用旧 mixer
  快照。该方案不宣称能在监督器被 SIGKILL 的瞬间自动恢复。

状态和日志位于 `/storage/apps/data/launcher/`。应用二进制在
`/storage/apps/current/`；退出应用不会删除其独立数据目录。

## 设备验证（由主任务串行执行）

使用实际连接的序列号，旧版 adbd 不宜并发开多个 shell。启动前先记录
`getprop init.svc.smartUI`、`pidof mp_s300` 和 `/proc/meminfo` 中的
`MemAvailable`。后台运行时必须脱离 ADB 会话，例如：

```sh
adb -s MagicPen-931f06 shell 'mkdir -p /storage/apps/data/launcher; nohup setsid /storage/apps/current/launcher/run.sh </dev/null >/storage/apps/data/launcher/run.log 2>&1 & sleep 1'
```

随后检查 `run.log`、`run.lock/pid`、`run.lock/child`。自定义桌面出现时，
`init.svc.smartUI` 必须为 `stopped`，`pidof mp_s300` 必须无结果；再采样
`MemAvailable`。启动播放器时确认 `init.svc.mediaserver` 仍为 `running`。

至少验证：

1. 点 EXIT：原装桌面恢复为 running，`mp_s300` 存在，`run.lock` 消失。
2. 再次进入，对 `run.lock/child` 中记录且身份核验一致的 launcher 发 TERM：
   恢复结果应相同。使用 KILL 模拟 launcher 崩溃时，先保持在应用网格，避免
   将“子应用自身的父进程死亡处理”混入这一项验证。
3. 连续启动两次监督器：第二次退出并报告已运行，不影响第一个前台。
4. 从进入前已停止 smartUI 的状态测试一次：退出后仍为 stopped；测试结束后
   明确执行 `setprop ctl.start smartUI`，恢复用户原桌面。

只通过已核实身份的 PID 结束本次会话，禁止 `killall sh`、`killall mplayer`
或对所有厂商程序执行批量 kill。当前无原装桌面新图标时，以上启动命令仅是
调试入口；原装桌面图标注册不由本监督器实现。

脚本已通过 `sh -n`，并在隔离的 Debian 容器中使用替代服务命令检查：正常
退出、launcher 错误退出、监督器 TERM、重复启动拒绝、保留原停止状态、遗留
锁恢复、PID 起始时间不符时不误杀、原 UI 停止失败时拒绝交接。上述检查覆盖
监督逻辑，不能代替真机 framebuffer、媒体和内存回收测试。

加入音量守护器后，同一隔离回归另验证了：守护器崩溃时回退、launcher 被
KILL 后回收守护器，以及 ALSA 恢复前守护器必须已退出。音量程序的模拟输入
和控件测试覆盖双设备、150 ms 节流、真实上下界、双声道、读取失败不写入。
