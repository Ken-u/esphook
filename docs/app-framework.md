# esphook 应用与小游戏框架

## 快速开发

从仓库根目录开始：

```bash
python3 scripts/new-app.py counter --title '计数' --pc
python3 scripts/check-apps.py
idf.py set-target esp32c3
idf.py build
```

生成 `apps/counter/manifest.json`、`firmware/app.c` 和可选 `pc/main.py`。构建自动发现 manifest，启动器、应用数量和可选待机列表都读取同一注册表，**不用改中央分派代码**。应用静态编译进固件，PC 数据和应用消息写 RAM；这不是任意 C 代码的动态加载器。

直接让支持项目 skills 的 Agent 使用 `$esphook-app` 开发设备应用、`$esphook-pc` 开发配套、`$esphook-check` 验证。其他工具可以直接读取 `AGENTS.md` 中列出的 SKILL.md。生成器模板也放在对应 skill 的 assets 中。

## 目录与接口

| 路径 | 职责 |
| --- | --- |
| `framework/include/esphook/app.h` | 稳定应用契约、descriptor、事件、导航和注册表访问 |
| `framework/core/app_manager.c` | 纯 C 生命周期、状态 arena、导航与渲染节拍 |
| `framework/core/app_keys.c` | 实体/USB 输入合并、映射与 USB 租期取消 |
| `framework/core/app_input.c` | 25ms 消抖、500ms 确认、800ms 三击、松手消费 |
| `framework/include/esphook/app_ui.h` | LVGL 标签、矩形、图层、字体、确认圆与共享 RGB565 画布 |
| `framework/include/esphook/app_services.h` | 雷达、舵机、Hook、显示协议、设备信息、时钟、随机数、PC 事件 |
| `framework/ports/esp32` | RTOS 队列、NVS 适配、异步 Hook 回传、LVGL 与 LCD 输出 |
| `framework/services` | Hook 通知模型、人体跟随/校准、显示协议与雷达投影 |
| `framework/drivers` | LD2450 UART/解析、NHY009 LEDC 输出 |
| `framework/pc/esphook_sdk` | 共用 USB 传输与通用应用协议 |
| `apps/<id>/firmware` / `pc` | 应用实现与 PC 配套，各自包含需要的模型、协议与测试 |
| `cmake/apps.cmake` | 按 manifest 自动生成注册表，固件与无屏测试共用 |

`main/` 只保留启动、配置/网络/HTTP/USB 接入、字体资源与任务入口。`display_task` 不知道具体应用 id，也不再调用应用专用渲染函数。`input_task` 只提交消抖后的按下/松手，不执行 Hook 或游戏逻辑。原 `host/*.py` 三个入口及已注册的 `host/hooks` adapter 保留兼容跳转，新代码放 apps 中。

## 应用契约

```c
const app_descriptor_t esphook_app_counter = {
    .api_version = APP_API_VERSION,
    .id = "counter", .title = "计数",
    .refresh_ms = 33, .state_size = sizeof(state_t),
    .on_enter = enter, .on_leave = leave,
    .on_event = event, .on_tick = tick,
    .render = render, .icon = icon
};
```

| 回调 / 数据 | 契约 |
| --- | --- |
| `init()` | 每次启动调用一次，USB 接收前初始化；此时没有 LVGL，不能建控件；可分配应用的常驻传输资源 |
| `on_enter(ctx)` | 新会话；state 已清零，root 已创建；在 root 下建控件 |
| `on_leave(ctx)` | 释放应用资源、图片缓存等；随后框架释放服务租约、删除 root 及动画、清零 state |
| `on_event(ctx, e)` | DOWN / UP / SHORT / CONFIRM / CANCEL / MESSAGE / NOTIFY / NETWORK；text 只在回调期间有效 |
| `on_tick(ctx)` | 每个 UI 循环约 5ms 调用，推进非阻塞逻辑；用 now_ms 算时间，不按帧计时 |
| `render(ctx)` | 根据 refresh_ms 和脏状态绘制；菜单 16ms、投篮 33ms、表情/做 T 40ms；帧率需实屏测量 |
| `icon(parent,x,y,selected)` | 在提供的 28×28 区域画代码图标；不能保存 parent 地址或访问当前应用 state |
| `command(line)` | 唯一从 USB 任务调用的回调；只解析并向自己的线程安全模型/队列写数据，禁止 LVGL 和读取 UI state |
| `ctx->state` | 当前应用独占、按 max_align_t 对齐的 2048 字节 arena；state_size 超限拒绝启动；退出后地址内容失效 |
| `ctx->root` | 框架拥有的 LVGL 根节点；应用不能删除 root，不能在 lv_screen_active 下另建应用节点 |

除 init 和 command 外，回调都由 UI 任务调用。导航 `app_open/menu/idle` 延迟到回调返回提交；不允许把 ctx 或事件临时指针留给后台任务。回调中不等待、联网或执行长计算；传输模型需要跨任务时独立同步，例如 stock_transport 的互斥锁。

所有应用共用 2048 字节会话区，而不是每个应用预留一份。雷达和 K 线共用 15,360 字节像素缓冲。共享画布每次使用都重绘，切出前清理图片缓存，参见雷达/股票的 on_leave。常驻股票上传暂存区仍在应用自己的 transport 中，只有完整数据才提交。

## 注册与应用配置

```json
{"id":"counter","order":"500","required":false,"sources":["firmware/app.c"]}
```

目录名、id 和 `esphook_app_<id>` 符号一致。id 为 1..23 位小写字母/数字/下划线，首位字母；order 三位数字，按值及路径稳定排序。sources 只列该应用内的真实 C 源文件。标题一格 48px，建议最多四个汉字或八个 ASCII 字符。

可用 `idf.py -DESPHOOK_APP_demo=OFF build` 移除可选应用，减少体积。face、launcher、settings 是必需应用。新增应用无屏测试自动发现；禁用某应用时其专用 USB 命令也不再注册。

| flags | 语义 |
| --- | --- |
| `APP_HIDDEN` | 系统应用，不占启动器、设置应用计数；launcher 使用此标记 |
| `APP_STANDBY` | 可作为待机应用，当前有 face、hook、radar |
| `APP_NOTIFICATIONS` | 待机收到通知时切到此应用，当前由 hook 使用；进行中的游戏、设置不会被抢占 |

## 全局输入与导航

GPIO6 内部上拉、按下接 GND，双边沿 ISR 只唤醒输入任务。通用固件默认 auto 保留 GPIO6/10/11；screen 也保留三键；只有显式选择 robot 并重启才仅启用 GPIO6，将 GPIO10 交给舵机。实体/USB 均先经 app_keys 映射和来源合并，再使用同一手势框架。K2/K3 只给应用普通输入，全局三击仅识别 K1。

- 短按在消抖后的松手时产生，按住未满 500ms；没有确认等待。投篮据此判定出手。
- DOWN 后 500ms 产生一次 CONFIRM，之后松手只有 UP，不再产生 SHORT。做 T 用 DOWN 暂停、CONFIRM 买入、UP 恢复/避免误卖。
- 800ms 内第三次有效按下优先回 launcher，第三次松手被消费。为保持即时短按，前两次可能已经执行当前应用动作。
- 导航提交会消费正在按住的键，避免长按进入后松手触发新应用。
- 待机应用中的 K1 短按统一开菜单；非待机入口打开同一应用时由应用处理短按。三击始终保留。
- 启动器按注册表每页三个图标，短按循环选择，500ms 确认圆填满进入；10s 无输入回当前选定的待机应用。

USB 按住租期为 350ms，SDK 通过 hold/press/poll 每 100ms 续期；失联、失焦或主动 release_keys 产生 CANCEL，不产生 SHORT/CONFIRM。应用用 CANCEL 清理临时按住状态，如恢复 K 线计时，不能成交。映射在下次 DOWN 生效，现有 UP 保留旧映射。详见 [PC 协议](app-pc-protocol.md)。

待机 id 保存在 NVS `apps/standby`，默认 face。保存失败不更新内存设置；如果升级后 id 不存在或不支持待机，启动时回退 face。恢复出厂也清理该设置。

## 硬件与 Hook 服务

`app_radar_read` 返回三目标快照、在线状态、age、帧计数和显示范围，应用不接触 UART。`app_servo_read` 返回运动状态；`app_servo_request(ctx, action, value)` 排队给运动服务，支持 ARM / DISARM / FOLLOW / CENTER / ANGLE。首次调用申请当前应用的舵机租约；退出强制 disarm，队列满也可提交停止请求。未装机器人硬件时返回不可用，应用仍可运行屏幕部分。

硬件策略由 app_hardware_policy 决定，auto 仅被动接收雷达 RX，screen 不启动外设，robot 才初始化 PWM/UART。app_hardware_info 提供实际运行/保存配置和各端口状态；NHY009 和屏幕均无读回，不能把初始化当作检测到实物。配置保存需重启生效，详见 [发布与兼容](release-kit.md)。

舵机默认上电无 PWM，校准、1000..2000µs 限制、机械角限位、速度限制和雷达失联策略继续由后台服务执行。普通屏幕应用只读取雷达/舵机，不改变先前通过 USB 设置的跟随状态。

Hook 的 HTTP `/notify`、`/dismiss`、配对、鉴权和 device link 协议保留。两种硬件都会启动可选 device link；无配置时等待。通知模型只在 UI 任务修改，`app_hook_read` 给应用快照，`app_hook_reply` 复制 client/session/word/callback 后入后台队列，网络不阻塞 UI。回传被 device link 队列接受或 HTTP 返回 2xx 后才提交 dismiss；失败保留通知并输出错误，不把 queued 当作主机已执行。

## 内置应用与设置

| 应用 | 实现 |
| --- | --- |
| 表情 | 自动眼睛动画、自定义表情/文字、人体方向状态 |
| 投篮 | 设备端纯 C 规则，两分钟、随机速度、计分 |
| 做 T | PC 行情/均线/交易/结算；设备端 RAM 数据、图表和按键协议 |
| 雷达 | 三目标扇形调试图，服务快照 |
| Hook | 通知展示、未读信息、选择回复词及确认回传 |
| 按键 | USB `app_event` 回传词；复用 Hook 回传服务 |
| 设置 | 六页：固件版本/构建时间、应用数量、Hook 信息、待机设置、硬件状态、硬件配置 |
| 示例 | 点击计数、清零、动画、雷达状态和 PC 自定义标题 |
| 启动器 | 隐藏的系统应用，代码图标、分页、选择动画和确认圆 |

设置短按循环六页。硬件配置页长按编辑、短按切换、长按保存，再长按重启生效。待机页长按进入编辑，短按切换可选应用，长按保存；三击随时回菜单。固件版本取 ESP-IDF app descriptor，当前版本由 `version.txt` 管理。数量统计已编译的可见应用，默认 8 个，不含系统 launcher；不显示凭据或认证密钥。

PC 的运行入口和协议见 [app-pc-protocol.md](app-pc-protocol.md)。旧投篮命令仍可用，start 排队打开新局，stop 回菜单，status 查询最近快照；`robot menu` 通用开菜单。

## 验证

```bash
python3 scripts/check-apps.py
python3 tests/test_scaffold.py
python3 tests/test_app_sdk.py
./scripts/test-app-games.sh
./scripts/test-robot.sh
./scripts/test-hardware.sh
./scripts/test-basketball.sh
./scripts/test-robot-ui.sh
python3 apps/stock/pc/test_stock_t.py
python3 apps/hook/pc/test_hooks.py
python3 apps/hook/pc/test_device_link.py
```

纯 C 测试需要 C 编译器和 `libcjson-dev`。其他环境通过 CJSON_INCLUDE_DIR / CJSON_LIB 指定；启用 ASan/UBSan。应用生成器会拒绝覆盖同名目录及不合法 id。

真实 LVGL 无屏渲染（cmake 3.19+，C/C++ 编译器）：

```bash
git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git /tmp/esphook-lvgl
LVGL_SOURCE_DIR=/tmp/esphook-lvgl ./scripts/test-app-ui.sh
```

它使用现有 fontdata 和 64KB LVGL 堆，遍历所有 manifest 应用、重复切换、检查根节点释放、设置持久化回调与 PC 消息，输出 PPM 屏幕帧。CI 上传这些帧，额外构建 ESP-IDF 5.5.4 默认通用配置与禁用机器人驱动的旧 Hook 配置。可选应用的完整硬件行为仍需要实机确认；主机 UI 测试不会驱动舵机，也不能证明实际 FPS。

`APP_UI_BUILD_DIR` / `APP_UI_FRAMES_DIR` 可覆盖无屏测试目录。CI 的固件尺寸检查沿用两个 OTA 槽；先看实际体积再决定是否取消 OTA。未新增固件图片或大型游戏依赖。
