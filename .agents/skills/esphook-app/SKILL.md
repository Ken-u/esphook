---
name: esphook-app
description: Develop native apps and mini games in the esphook ESP32 project using the application lifecycle, shared LVGL UI, global input, radar, servo, buzzer and Hook interfaces. Use for adding, changing or migrating device apps under apps/id/firmware and their manifests.
---

# 开发设备应用

在仓库根目录操作。先读 `docs/app-framework.md`（契约、公共 API 速查、验证），再按功能读 `framework/include/esphook/app.h`、`app_ui.h`、`app_services.h`，以及速查表里列出的 `app_math.h`、`app_rand.h`、`app_sync.h`、`app_command.h`、`app_best.h`。参考实现：`apps/demo/firmware/app.c`（最小）、`apps/screentest`（K1 驱动的测试页）、`apps/buzztest`（用公共服务发声）、投篮和做 T（独立模型 + PC 分工）。

## 1. 起步

1. `python3 scripts/new-app.py <id> --title '<最多四个汉字>' [--pc]` 生成 `apps/<id>/manifest.json`、`firmware/app.c`（和可选 `pc/main.py`）。保留 manifest 里真实的源文件列表；CMake 自动发现，**不要改中央分派器或注册数组**。
2. 只能 `#include` C 标准库、cJSON、自己的头和 `esphook/app*.h`；不要碰 FreeRTOS、ESP-IDF、`app_runtime.h`（`scripts/check-apps.py` 会拒绝）。
3. 逻辑放 `apps/<id>/firmware`：用 descriptor 的 `on_enter/on_leave/on_event/on_tick/render` 实现生命周期；可单测的规则和 LVGL 绘制分开，核心保持纯 C 以便主机测试。

## 2. 状态、UI 与资源

- 会话状态放 `ctx->state`，用 `_Static_assert` 检查 2048 字节上限；控件只在 `ctx->root` 下创建，退出后不再持有控件或状态地址。
- 优先复用 `app_ui`：字体、代码图标、确认圆；共享画布 `app_ui_canvas(root,x,y,w,h)` + `app_ui_canvas_refresh()`，画布下 16px 状态条 `app_ui_band()`，按住进度线 `app_ui_hold_bar()`。整数数学、确定性随机、固定步长（`app_math.h`、`app_rand.h`、`app_step_take`）不要在应用里再写一份。
- 要立体感（角色、道具、枪）用 `app_stack.h` 的切片堆叠（`docs/stack-render.md`；参考 `apps/zombie`、`apps/racing`、`apps/starship`）：固定摆在世界里的东西用 `stack_yaw_seen` 算朝向，近处用体素、远处退回平面精灵，用 `stack_draw` 的返回值做预算和状态里的 `vox=`。
- 屏幕上的每个汉字必须在固件字库里（缺字显示成空白）：无屏渲染测试会逐个 label 检查；只用字库覆盖的字。
- 菜单标题保持短小，长正文用滚动标签；避免每帧分配内存、阻塞等待或联网；共享画布仅当前应用使用。

## 3. 输入

- 接收 `APP_EVENT_SHORT` 和 `APP_EVENT_CONFIRM`；需要暂停时再接 DOWN/UP，并处理 `APP_EVENT_CANCEL`（恢复临时暂停、不成交、不当作短按）。实体键和 USB 虚拟键共用这些事件，USB 失联或租期到期会取消。
- 复用全局 25 ms 消抖、500 ms 确认、800 ms 三击导航；**不要在应用里另写 GPIO 或三击/双击逻辑**。双击回上一个应用也是框架的：`app_back(ctx)` 是程序接口，`app_set_double_back(ctx,false)` 单独关闭，屏蔽三击时双击随之关闭。
- 靠连按的游戏在 `on_enter` 调 `app_set_triple_exit(ctx,false)` 屏蔽三击（切换应用自动恢复），并用 `app_set_exit_hold(ctx,2000)` 留一个按住 2 秒的出口（平静画面开启、战斗中关闭；框架只统计按下时出口已开启的那一按）；提示写“按住2秒退出”，进度线用 `app_ui_hold_line()`。
- 导航请求延后到回调返回才提交，提交会消费本次松手。
- 最高分：进入时 `core.best = app_best_get(ctx)`，一局结束时 `app_best_record(ctx, core.best)`（`app_best.h`，按应用 id 自动分库，只在破纪录时写 flash，结算画面每帧调用也安全；不要每涨一分就记，也不要自己拼 `app_storage` 的键）。
- 应用设置用 `app_storage_load/save`（≤64 字节、带版本号和范围校验、只在玩家修改时写）。

## 4. 硬件（只通过公共服务）

- **雷达**：`app_radar_read`（快照、在线状态、age）。**舵机**：`app_servo_read`，`app_servo_request` 排队控制。用 `app_hardware_info` 查询运行配置和状态；`available/online` 区分驱动支持和收到有效帧，屏幕和 PWM 舵机没有反馈，不要伪报实物检测。
- **舵机安全**：只有明确的用户确认动作才 ARM，从不自动上电动作；应用退出释放舵机租约并 disarm。后台跟随服务保留校准、限位和限速，校准用 `app_servo_cal_read/apply`（舵机已通电时也生效但不会通电，参考 `apps/servocal`），不要自己改运动服务的配置。启用舵机只能由玩家在标题/待机画面长按 500 ms 触发，并保证游戏中的按键不触发启用（参考 `apps/starship`、`apps/racing`）。
- **第一人称街机游戏**（标题/结算画面、按住 2 秒退出、状态条、雷达或 K2/K3 转向）先用 `app_game.h`，不要再抄一份 HUD/退出/转向代码（参考 `apps/racing`）；游戏要摇舵机或把舵机当方向盘用 `app_haptic.h`（按限位和限速换算，缺失或未启用时静默），雷达/按键左右转向用 `app_steer.h`。
- **蜂鸣器**：`app_beep(APP_BEEP_*)`（只入队、不阻塞，由 `beeper` 任务在 GPIO7 无源蜂鸣器上播放，静音时无声，音型表见 `app_sound.h`）；单个音符 `app_beep_tone(hz, ms)`；静音开关 `app_beep_enabled()` / `app_beep_enable(on)`（写 flash，只在玩家明确选择时调）。**不要自己碰 LEDC**：timer 0 / channel 0 是蜂鸣器，timer 1 / channel 1 是舵机（`hw_pwm.h`）。设备上直接试听见 `apps/buzztest`。
- **Hook 与 PC**：`app_hook_read/reply` 复用通知和异步回传；`APP_EVENT_MESSAGE` 接收 PC 消息，事件 text 只在回调期间有效。网络、历史数据和大型计算放 `apps/<id>/pc`（见 `esphook-pc` 与 `docs/app-pc-protocol.md`）。

## 5. 待机、开机流程与闹钟

- 给可选应用设置 `APP_STANDBY` 才会出现在待机选择里；再加 `APP_DEFAULT_STANDBY` 成为未保存选择时的默认待机（全工程最多一个，现为 `wedding`）。`APP_NOTIFICATIONS` 指定待机收到通知时的接收应用，`APP_HIDDEN` 隐藏系统入口。
- 开机前必须先做的设置（例如没保存过屏幕调整时先把屏幕调对）用可选的 `first_run()`：启动时问一次，答 `true` 的第一个应用排在待机之前，进入时 `ctx->first_run` 为真，做完调 `app_idle(ctx)`（框架接着按目录顺序问后面的应用，都问完才进待机）；自己防止误退出（关三连按）并设无人操作超时，参考 `apps/screentest`、`apps/servocal`。
- 要在不在屏幕上时被打开（闹钟、整点报时、游戏里的定时提醒）用全局闹钟 API `esphook/app_alarm.h`，不要自己轮询时钟：在 `init()` 里先 `app_alarm_cancel_owner(自己的 id)` 再 `app_alarm_add(&spec)` 登记（CLOCK 每个本地时间、AT 绝对时刻、AFTER 登记后 N 秒，后两者不需要校时；`idle_only` 只在待机时响；`target` 缺省是自己，也可指向 `"alarm"` 借用它的响铃画面）。到点框架打开目标应用并发 `APP_EVENT_ALARM`（`e->key` 是条目 id、`e->text` 是 label）；响铃时用 `app_alarm_snooze/done` 回答，离开应用就算 done，设置变了再 `app_alarm_update`。注册表只在 UI 任务和 `init()` 里碰；它在 RAM，重启后靠 `init()` 重新登记。时间用 `app_time_read()`（未校时 `valid=false`，必须处理）。舵机仍然只能由玩家明确启用：闹钟的“舵机提醒”开关是持久的明确选择，只作用于它自己的条目，是唯一的例外（参考 `apps/alarm`）。

## 6. USB 命令前缀

自己认领前缀：`command(line)` 在 USB 任务里**只校验、答复**（`app_command_is/reply/replyf`）、**请求切换**（`app_command_open/menu`）；状态由 UI 任务用 `app_shared_copy` 发布一份纯数据快照再读取（`app_sync.h`）。参考 `apps/mirror` 和生成的模板。

## 7. 验证

按 `docs/app-framework.md` 的验证步骤，先 `./scripts/check-all.sh`（见 `esphook-check`）。主机渲染通过不等于实机帧率、按键手感或声音/舵机表现已验证，交付时分开写。
