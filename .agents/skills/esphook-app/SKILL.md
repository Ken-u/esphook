---
name: esphook-app
description: Develop native apps and mini games in the esphook ESP32 project using the application lifecycle, shared LVGL UI, global input, radar, servo and Hook interfaces. Use for adding or migrating device apps under apps/id/firmware and their manifests.
---

# 开发设备应用

在仓库根目录操作。先阅读 `docs/app-framework.md`，再按功能读取 `framework/include/esphook/app.h`、`app_ui.h`、`app_services.h`。参考 `apps/demo/firmware/app.c`；投篮和做 T 展示独立模型及 PC 分工。

1. 用 `python3 scripts/new-app.py <id> --title '<最多四个汉字>' [--pc]` 创建应用。检查新目录，保留 `manifest.json` 中的真实源文件列表。CMake 自动发现，无需修改中央分派器或注册数组。
2. 将逻辑放在 `apps/<id>/firmware`。用 descriptor 的 `on_enter/on_leave/on_event/on_tick/render` 实现生命周期；把可单测的规则与 LVGL 绘制分开。
3. 把本次会话状态放在 `ctx->state`，用 `_Static_assert` 检查 2048 字节上限。仅在 `ctx->root` 下创建控件，退出后不能再持有控件或状态地址。字体、代码图标、确认圆和绘图缓冲优先复用 `app_ui`；图像描述符退出前清缓存，参考雷达应用。
4. 接收 `APP_EVENT_SHORT` 和 `APP_EVENT_CONFIRM`，需要暂停时再接 DOWN/UP，并处理 APP_EVENT_CANCEL：恢复临时暂停、不成交、不当作短按。实体/USB 共用此事件，USB 失联或租期到期会取消。复用全局 25ms 消抖、500ms 确认、800ms 三击导航，不在应用中另写 GPIO 或三击逻辑。靠连按的游戏在 `on_enter` 调 `app_set_triple_exit(ctx, false)` 屏蔽三击（切换应用自动恢复），并用 `app_set_exit_hold(ctx, 2000)` 留一个按住 2 秒的出口（只在这次按下不改变画面时开启；按住开始于战斗中的不要开），提示用“按住2秒退出”、进度线用 `app_ui_hold_line()`。导航请求延后到回调返回才提交，提交会消费本次松手。
5. 通过 `app_radar_read` 读快照，通过 `app_servo_request` 排队控制舵机。用 app_hardware_info 查询运行配置/状态，available/online 区分驱动支持和收到有效帧；屏幕和 PWM 舵机无反馈，不伪报实物检测。只有明确的用户确认动作才 ARM；从不自动上电动作。应用退出释放舵机租约并 disarm。后台跟随服务保留校准、限位和限速。游戏要摇舵机或把舵机当方向盘时用 `app_haptic.h`（按限位/限速换算，缺失或未启用时静默），雷达/按键左右转向用 `app_steer.h`；启用舵机只能由玩家在标题/待机画面长按 500ms 触发，并保证游戏中的按键不触发启用（参考 `apps/starship`、`apps/racing`）。
6. 要在不在屏幕上时被打开（闹钟、整点报时、游戏里的定时提醒）：用框架的全局闹钟 API `esphook/app_alarm.h`，不要自己轮询时钟。在 `init()` 里先 `app_alarm_cancel_owner(自己的 id)` 再用 `app_alarm_add(&spec)` 登记（CLOCK 每个本地时间、AT 绝对时刻、AFTER 登记后 N 秒不需要校时；`idle_only` 只在待机时响；`target` 缺省是自己，也可以指向 `"alarm"` 借用它的响铃画面），框架到点打开目标应用并发 `APP_EVENT_ALARM`（`e->key` 是条目 id、`e->text` 是 label）；响铃时用 `app_alarm_snooze/done` 回答，离开应用就算 done，设置变了再 `app_alarm_update`（说同样的话不会打断响铃）。注册表只在 UI 任务和 `init()` 里碰，USB `command` 回调不能直接调用；注册表在 RAM，重启后靠 `init()` 重新登记，设置用 `app_storage_load/save`（≤64 字节、带版本号和范围校验、只在玩家修改时写）；时间用 `app_time_read()`（未校时 `valid=false`，必须处理）。舵机仍然只能由玩家明确启用：闹钟的“舵机提醒”开关是持久的明确选择，只作用于它自己的条目，是唯一的例外，参考 `apps/alarm`。USB 命令前缀自己认领（`command(line)` 只校验、复制、入队，答复用 `app_transport_write`），参考 `apps/mirror`。
7. 用 `app_hook_read/reply` 复用通知与异步回传。用 `APP_EVENT_MESSAGE` 接收 PC 消息；事件 text 只在回调期间有效。PC 网络、历史数据和大型计算移到 `apps/<id>/pc`，参考 `docs/app-pc-protocol.md`。
8. 按 `docs/app-framework.md` 的验证步骤运行受影响模型测试、`scripts/check-apps.py`、框架测试和真实 LVGL 无屏测试。需要交付固件时构建机器人和 legacy 两种配置，记录槽位占用。主机渲染通过不等于实机帧率已验证。

给可选应用设置 `APP_STANDBY` 才会出现在待机选择中；再加 `APP_DEFAULT_STANDBY` 可成为未保存选择时的默认待机（全工程最多一个，现为 wedding）。`APP_NOTIFICATIONS` 指定待机收到通知时的接收应用，`APP_HIDDEN` 隐藏系统入口。菜单标题保持短小，长正文使用滚动标签。避免每帧分配内存、阻塞等待或联网；共享画布仅当前应用使用。
