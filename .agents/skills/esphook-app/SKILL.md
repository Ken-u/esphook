---
name: esphook-app
description: Develop native apps and mini games in the esphook ESP32 project using the application lifecycle, shared LVGL UI, global input, radar, servo and Hook interfaces. Use for adding or migrating device apps under apps/id/firmware and their manifests.
---

# 开发设备应用

在仓库根目录操作。先阅读 `docs/app-framework.md`，再按功能读取 `framework/include/esphook/app.h`、`app_ui.h`、`app_services.h`。参考 `apps/demo/firmware/app.c`；投篮和做 T 展示独立模型及 PC 分工。

1. 用 `python3 scripts/new-app.py <id> --title '<最多四个汉字>' [--pc]` 创建应用。检查新目录，保留 `manifest.json` 中的真实源文件列表。CMake 自动发现，无需修改中央分派器或注册数组。
2. 将逻辑放在 `apps/<id>/firmware`。用 descriptor 的 `on_enter/on_leave/on_event/on_tick/render` 实现生命周期；把可单测的规则与 LVGL 绘制分开。
3. 把本次会话状态放在 `ctx->state`，用 `_Static_assert` 检查 2048 字节上限。仅在 `ctx->root` 下创建控件，退出后不能再持有控件或状态地址。字体、代码图标、确认圆和绘图缓冲优先复用 `app_ui`；图像描述符退出前清缓存，参考雷达应用。
4. 接收 `APP_EVENT_SHORT` 和 `APP_EVENT_CONFIRM`，需要暂停时再接 DOWN/UP，并处理 APP_EVENT_CANCEL：恢复临时暂停、不成交、不当作短按。实体/USB 共用此事件，USB 失联或租期到期会取消。复用全局 25ms 消抖、500ms 确认、800ms 三击导航，不在应用中另写 GPIO 或三击逻辑。导航请求延后到回调返回才提交，提交会消费本次松手。
5. 通过 `app_radar_read` 读快照，通过 `app_servo_request` 排队控制舵机。用 app_hardware_info 查询运行配置/状态，available/online 区分驱动支持和收到有效帧；屏幕和 PWM 舵机无反馈，不伪报实物检测。只有明确的用户确认动作才 ARM；从不自动上电动作。应用退出释放舵机租约并 disarm。后台跟随服务保留校准、限位和限速。
6. 用 `app_hook_read/reply` 复用通知与异步回传。用 `APP_EVENT_MESSAGE` 接收 PC 消息；事件 text 只在回调期间有效。PC 网络、历史数据和大型计算移到 `apps/<id>/pc`，参考 `docs/app-pc-protocol.md`。
7. 按 `docs/app-framework.md` 的验证步骤运行受影响模型测试、`scripts/check-apps.py`、框架测试和真实 LVGL 无屏测试。需要交付固件时构建机器人和 legacy 两种配置，记录槽位占用。主机渲染通过不等于实机帧率已验证。

给可选应用设置 `APP_STANDBY` 才会出现在待机选择中；再加 `APP_DEFAULT_STANDBY` 可成为未保存选择时的默认待机（全工程最多一个，现为 wedding）。`APP_NOTIFICATIONS` 指定待机收到通知时的接收应用，`APP_HIDDEN` 隐藏系统入口。菜单标题保持短小，长正文使用滚动标签。避免每帧分配内存、阻塞等待或联网；共享画布仅当前应用使用。
