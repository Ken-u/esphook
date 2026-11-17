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
| `framework/core/app_screen.c` | 面板版本（plugin/plain）和 `robot screentest` 命令的纯函数解析，主机测试 `./scripts/test-app-screen.sh` |
| `framework/core/app_time.c` | 墙上时钟换算（公历、星期、时区偏移）的纯函数，闹钟和幸运签用 |
| `framework/core/app_alarm.c` | 全局闹钟注册表：任何应用登记“到点打开哪个应用并通知它”，框架负责判定、排队、贪睡与唤醒；见“闹钟 API” |
| `framework/include/esphook/app_ui.h` | LVGL 标签、矩形、图层、字体、确认圆（`app_ui_ring`，设置等应用用）；共享 RGB565 画布 `app_ui_canvas`、底部状态条 `app_ui_band`、按住进度线 `app_ui_hold_bar` |
| `framework/include/esphook/app_gfx.h` | 画布软件绘图：线/圆/圆环/椭圆光晕/渐变（抖动）、半透明叠加、调色板点阵、山脉/云/太阳，只在 UI 任务调用 |
| `framework/include/esphook/app_math.h` | 纯整数数学（无 LVGL/RTOS，可进 `*_core.c`）：abs/clamp/lerp、饱和加、开方、抛物线正弦、哈希、固定步长累加器 |
| `framework/include/esphook/app_rand.h` | 确定性 xorshift32：种子即序列，主机测试与实机一致 |
| `framework/include/esphook/app_sync.h` | UI 任务与 USB 任务交接数据：`app_shared_copy`、临界区、互斥锁；应用不再 include FreeRTOS |
| `framework/include/esphook/app_command.h` | USB 文本命令：匹配、回复、请求打开应用/回菜单 |
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
| `on_event(ctx, e)` | DOWN / UP / SHORT / CONFIRM / CANCEL / MESSAGE / NOTIFY / NETWORK / ALARM；text 只在回调期间有效；ALARM 的 key 是闹钟条目 id、text 是 label，见“闹钟 API” |
| `on_tick(ctx)` | 每个 UI 循环约 5ms 调用，推进非阻塞逻辑；用 now_ms 算时间，不按帧计时 |
| `render(ctx)` | 根据 refresh_ms 和脏状态绘制；菜单 16ms、投篮 33ms、表情/做 T 40ms；帧率需实屏测量 |
| `icon(parent,x,y,selected)` | 在提供的 28×28 区域画代码图标；不能保存 parent 地址或访问当前应用 state |
| `command(line)` | 唯一从 USB 任务调用的回调；只解析、回复、读取 UI 任务发布的快照（`app_command.h` + `app_sync.h`），禁止 LVGL 和读取 UI state |
| `ctx->state` | 当前应用独占、按 max_align_t 对齐的 2048 字节 arena；state_size 超限拒绝启动；退出后地址内容失效 |
| `ctx->root` | 框架拥有的 LVGL 根节点；应用不能删除 root，不能在 lv_screen_active 下另建应用节点 |

除 init 和 command 外，回调都由 UI 任务调用。导航 `app_open/menu/idle` 延迟到回调返回提交；不允许把 ctx 或事件临时指针留给后台任务。回调中不等待、联网或执行长计算；跨任务数据用 `app_sync.h`：状态快照 `app_shared_copy`，解析/较长的临界区（stock_transport）用 `app_lock_t`。

所有应用共用 2048 字节会话区，而不是每个应用预留一份。雷达、K 线、小恐龙、滑雪、投篮、新婚快乐、电子木鱼、水族箱、钓鱼、星际战机、极速赛车、黄金矿工、跳一跳、僵尸围城、幸运签、闹钟和副屏共用 160×64 的 20,480 字节像素缓冲（`APP_CANVAS_W/H`；雷达和 K 线只用其中 160×48），整屏重绘的游戏用 `app_gfx.h` 里的有裁剪绘图函数，用 `app_ui_canvas(root, x, y, w, h)` 显示（描述符归框架，应用 state 里只留返回的对象），重绘后 `app_ui_canvas_refresh()`；创建时框架会清掉上一个应用留下的图片缓存。常驻股票上传暂存区仍在应用自己的 transport 中，只有完整数据才提交。

## 公共 API 速查

应用只能 include 白名单里的头文件：C 标准库、cJSON、自己的本地头、`esphook/app*.h`（含 `app_math/rand/sync/command/best/screen/gfx/ui/haptic/steer/fish/alarm`）、`esphook/radar_view.h` 和 `robot_ui_protocol.h`。`check-apps.py` 对每个应用的 `firmware/` 逐文件检查，FreeRTOS、ESP-IDF、驱动或 `app_runtime.h` 一律拒绝（要什么就由框架出 API，而不是应用越层）。越稳定的 API 越能装更多应用，所以下面这些**不要在应用里再写一份**：

| 需求 | 用这个 | 备注 |
| --- | --- | --- |
| 绝对值/限幅/插值/开方/抛物线正弦/哈希 | `app_math.h`：`app_abs/min/max/clamp/lerp`、`app_isqrt`、`app_wave127`（±127）、`app_sin256`（±256）、`app_hash32` | 纯头文件、仅依赖 `<stdint.h>`，核心逻辑（`*_core.c`）可直接用，主机测试同步编译 |
| 计时器“距今多久”不回绕 | `app_age_add(age, dt)`（上限 `APP_AGE_MAX`）、`app_sat_add(v, d, max)` | 放置几小时的计数器也不会溢出 |
| 固定步长模拟 | `for (n = app_step_take(&carry, dt, STEP_MS, MAX_ADVANCE_MS); n; --n) step(g);` | 卡帧不会一次补太多；同输入逐位重放一致 |
| 随机数 | `app_rand_seed/next/range/chance`（`app_rand.h`），种子用 `app_random()` 取一次 | 状态是应用自己结构体里的 `uint32_t`，种子相同则序列相同 |
| 显示共享画布 | `app_ui_canvas(root, x, y, w, h)` + `app_ui_canvas_refresh(obj)` | state 不再放 `lv_image_dsc_t` |
| 画布下面 16px 状态条 | `app_ui_band(root, bg, rule)`（`rule` 传 `APP_UI_NO_RULE` 则无顶线） | 标签建在返回的对象里 |
| 按住退出/确认进度线 | `app_ui_hold_bar(parent, y, rgb)` + `app_ui_hold_line()` | 配合 `app_set_exit_hold` |
| UI 任务发布状态给 USB 任务读 | `app_shared_copy(dst, src)`；多字段/自增用 `app_critical_enter/exit()` | 临界区内只拷贝几个字节；不要 FreeRTOS |
| 需要解析、较长的共享区 | `app_lock_create/take/give`（互斥锁） | 例：股票传输模型 |
| 面板调整（颜色反相、RAM 偏移、SPI 时钟，跨重启） | `app_screen_read/apply/raw`（`esphook/app_screen.h`，线程安全，USB 任务也能调）和 `app_screen_parse`（`robot screentest ...` 文本命令的解析） | 新屏模块的两种版本见 `docs/hardware.md`；`screentest` 应用是它的界面 |
| 最高分/记录（跨退出、跨重启） | `app_best_get(ctx)`（进入时灌给核心）、`app_best_record(ctx, score)`（一局结束时调用） | 按应用 id 自动分库，只在破纪录时写一次 flash，每帧调用也安全；见“设置存储” |
| 回到上一个应用 | `app_back(ctx)`；全局手势“双击”已由框架处理 | 见“全局输入与导航” |
| 回复 USB 文本命令 | `app_command_is/reply/replyf`（一行，至多 `APP_COMMAND_REPLY_MAX` 字节） | `command()` 返回 true 表示这行是你的 |
| 从命令打开应用/回菜单/问是否前台 | `app_command_open(id)`、`app_command_menu()`、`app_command_active(id)` | 只排队，真正切换在 UI 任务 |
| 闹钟/定时唤醒 | `app_alarm.h` | 见“闹钟 API” |
| 舵机震动/转向、雷达转向 | `app_haptic.h`、`app_steer.h` | 见“硬件与 Hook 服务” |

一个只带 `status` 命令的完整样板就是 `python3 scripts/new-app.py <id>` 生成的 `firmware/app.c`：

```c
typedef struct { bool active; unsigned count; } status_t;
static status_t s_status;                       /* UI 任务发布，command（USB 任务）读副本 */
static void publish(const state_t *s, bool active) { const status_t v = {active, s->count}; app_shared_copy(s_status, v); }
static bool command(const char *line)
{
    if (!app_command_is(line, "robot counter status")) return false;
    status_t v;
    app_shared_copy(v, s_status);
    (void)app_command_replyf("active=%u count=%u", v.active ? 1U : 0U, v.count);
    return true;
}
```

共享状态必须是纯数据结构：不能含指向应用 arena 的指针（退出后失效）。无屏测试把 `app_critical_*` 替换成会断言“不嵌套、不泄漏”的桩，并要求每个应用都经由它发布过状态。

## 注册与应用配置

```json
{"id":"counter","order":"500","required":false,"sources":["firmware/app.c"]}
```

目录名、id 和 `esphook_app_<id>` 符号一致。id 为 1..23 位小写字母/数字/下划线，首位字母；order 三位数字，按值及路径稳定排序。sources 只列该应用内的真实 C 源文件。标题一格 48px，建议最多四个汉字或八个 ASCII 字符。

可用 `idf.py -DESPHOOK_APP_demo=OFF build` 移除可选应用，减少体积。face、launcher、settings 是必需应用。新增应用无屏测试自动发现；禁用某应用时其专用 USB 命令也不再注册。

| flags | 语义 |
| --- | --- |
| `APP_HIDDEN` | 系统应用，不占启动器、设置应用计数；launcher 使用此标记 |
| `APP_STANDBY` | 可作为待机应用，当前有 wedding、face、hook、radar、alarm |
| `APP_DEFAULT_STANDBY` | 与 `APP_STANDBY` 一起使用：NVS 没有保存待机、或保存的应用已不存在时的默认待机，当前是 wedding；整个工程最多一个，没有则回退 face |
| `APP_NOTIFICATIONS` | 待机收到通知时切到此应用，当前由 hook 使用；进行中的游戏、设置不会被抢占 |

## 全局输入与导航

GPIO6 内部上拉、按下接 GND，双边沿 ISR 只唤醒输入任务。通用固件默认 auto 保留 GPIO6/10/11；screen 也保留三键；只有显式选择 robot 并重启才仅启用 GPIO6，将 GPIO10 交给舵机。实体/USB 均先经 app_keys 映射和来源合并，再使用同一手势框架。K2/K3 只给应用普通输入，全局三击仅识别 K1。

- 短按在消抖后的松手时产生，按住未满 500ms；没有确认等待。投篮据此判定出手。
- DOWN 后 500ms 产生一次 CONFIRM，之后松手只有 UP，不再产生 SHORT。做 T 用 DOWN 暂停、CONFIRM 买入、UP 恢复/避免误卖。
- 800ms 内第三次有效按下优先回 launcher，第三次松手被消费。为保持即时短按，前两次可能已经执行当前应用动作。
- 导航提交会消费正在按住的键，避免长按进入后松手触发新应用。
- **可屏蔽三击**：靠连按玩的游戏（开炮、投喂、敲木鱼、跳跃）在 `on_enter` 里调用 `app_set_triple_exit(ctx, false)`，快速三次按下就只是三次普通按下，不会再回菜单；`app_triple_exit(ctx)` 查询当前状态。该开关在**每次切换应用时恢复为开启**，忘记调用不会让下一个应用失去三击，也不用在 `on_leave` 里还原。
- **屏蔽后的出口**：长按 K1 达到指定时间回菜单，`app_set_exit_hold(ctx, ms)` 开启（1000..10000ms，0 关闭，默认关闭，同样随应用切换复位），`app_exit_hold_progress(ctx)` 给出已按住的毫秒数，`app_ui_hold_line()` 画 2px 进度线（有 500ms 确认时金色先填满，之后红色填到出口；无确认时 600ms 之前不显示，普通按键不闪）。应用仍会收到 DOWN，500ms 时仍有 CONFIRM；到时回菜单并消费这次按住，松手无动作。**框架只统计“按下时出口已开启”的那一按**：战斗中关闭、平静画面开启，那么开局那一按、或战斗里一直按着的开火键在结算画面一松都不会变成退出；按住途中才开启（或关闭后重新开启）要等下一次按下才生效，应用不用再自己记“这一按开始于游戏中”。目前：星际战机、极速赛车、钓鱼、僵尸围城只在平静画面开启（2 秒）；水族箱、木鱼、投篮、小恐龙、黄金矿工随时开启（2 秒），滑雪 3 秒；投篮结算页任何短按仍直接回菜单。跳一跳靠计时按住而不是连按，**保留全局三击**，没有按住退出（蓄力要按住很久）；副屏、闹钟、幸运签也保留三击。
- **双击回上一个应用**：K1 两次点击间隔不超过 `APP_DOUBLE_MS`（300ms），再安静 300ms（期间没有第三次按下），框架就回到“上一个应用”，所以实际在第二次点击后约 0.3 秒生效。等这 300ms 是为了让快速三击仍然是三击（回菜单）而不是先被双击带走；三次按下落在这 300ms 内就是三击。两次点击都会作为普通按键先送给当前应用（和三击的前两次一样），之后才被带走。三次按下的节奏很不均匀（前两下 ≤0.3s，第二、三下之间却超过约 0.35–0.4s，按的时间越长越宽一点）会被读成双击，匀速三击和间隔 >0.3s 的慢速按键不受影响。
- “上一个应用”：框架记住最近两个**非菜单**应用，从 B（在 A 之后打开）双击回 A，再双击回 B，是应用切换器的来回切；在菜单里双击回到打开菜单时所在的应用，菜单光标不动（这两下点击本来会让光标走两格，框架会还原）。手势按**起手那一屏**计算：在待机屏上双击，第一下打开菜单、第二下落在菜单里，目标仍是“待机之前用的那个应用”而不是刚离开的待机屏本身；刚开机还没有上一个应用时什么也不发生。回到的若是待机应用，仍按待机屏恢复（短按开菜单、整点报时照响）。`app_back(ctx)` 是同一个动作的程序接口（任何应用都可以做一个“返回”按钮），没有上一个应用时返回 false。
- **双击和三击一起屏蔽/单独屏蔽**：`app_set_triple_exit(ctx, false)` 同时关掉双击（靠连按的应用两者都不要）；想保留三击、只关双击（例如应用自己要用快速双击）调用 `app_set_double_back(ctx, false)`；`app_double_back(ctx)` 查询当前是否生效。两个开关都在**每次切换应用时恢复**。双击要求点击是真正的点击（按住的 500ms 确认、慢速点击、被 USB 取消的按键都不算），其他键 K2/K3 从不触发。
- 待机应用中的 K1 短按统一开菜单；非待机入口打开同一应用时由应用处理短按。三击始终保留。
- 启动器是一个 iPod 式的列表：左边四行、蓝色高亮条在行间滑动、行数多于四行时列表整行滚动并有一条细滚动条，右边是选中应用的图标预览，顶栏是标题和 `n/N`；短按下移（到底回到第一个），500ms 确认时**高亮条自左向右被填满**并在条下长出一条金线，预览板的描边变金，填满即进入；10s 无输入回当前选定的待机应用。

USB 按住租期为 350ms，SDK 通过 hold/press/poll 每 100ms 续期；失联、失焦或主动 release_keys 产生 CANCEL，不产生 SHORT/CONFIRM。应用用 CANCEL 清理临时按住状态，如恢复 K 线计时，不能成交。映射在下次 DOWN 生效，现有 UP 保留旧映射。详见 [PC 协议](app-pc-protocol.md)。

待机 id 保存在 NVS `apps/standby`；没有保存时使用带 `APP_DEFAULT_STANDBY` 的应用（现在是 wedding，`app_manager_default_standby()` 解析），没有这样的应用则回退 face。保存失败不更新内存设置；如果升级后保存的 id 不存在或不支持待机，启动时同样取默认待机。恢复出厂也清理该设置。

## 硬件与 Hook 服务

`app_radar_read` 返回三目标快照、在线状态、age、帧计数和显示范围，应用不接触 UART。`app_servo_read` 返回运动状态；`app_servo_request(ctx, action, value)` 排队给运动服务，支持 ARM / DISARM / FOLLOW / CENTER / ANGLE。首次调用申请当前应用的舵机租约；退出强制 disarm，队列满也可提交停止请求。未装机器人硬件时返回不可用，应用仍可运行屏幕部分。`app_servo_read` 还给出配置的限位 `limit_deg` 和限速 `speed_dps`。

游戏想“摇一下舵机”或“把舵机当方向盘”，使用头文件 `framework/include/esphook/app_haptic.h`：它只通过公开舵机服务发 ANGLE 请求，按限位减 1° 钳幅度，按限速拉长半周期，两次请求至少间隔 60ms；舵机缺失、未启用或故障时一律静默。**框架不会自动启用舵机**：只有玩家在应用的待机/标题画面长按 500ms 确认后，应用才可调用 `haptic_arm()`。左右转向（雷达，或 K2/K3 两个键作后备）使用 `app_steer.h`：雷达按站在机器前面的人的视角处理（传感器 +X 是它自己的右边，面对机器的人的右边是传感器的 −X，所以内部取反，向你的右边侧身就是向右转，和雷达页一致）；以开局时最近的人为中心，侧身约 35cm 满舵，目标丢失保持 1.2 秒再回中，另一个人经过不会抢控制权。两者都是对时间戳的纯函数，主机测试见 `scripts/test-haptic-steer.sh`。

硬件策略由 app_hardware_policy 决定，auto 仅被动接收雷达 RX，screen 不启动外设，robot 才初始化 PWM/UART。app_hardware_info 提供实际运行/保存配置和各端口状态；NHY009 和屏幕均无读回，不能把初始化当作检测到实物。配置保存需重启生效，详见 [发布与兼容](release-kit.md)。

舵机默认上电无 PWM，校准、1000..2000µs 限制、机械角限位、速度限制和雷达失联策略继续由后台服务执行。普通屏幕应用只读取雷达/舵机，不改变先前通过 USB 设置的跟随状态。

Hook 的 HTTP `/notify`、`/dismiss`、配对、鉴权和 device link 协议保留。两种硬件都会启动可选 device link；无配置时等待。通知模型只在 UI 任务修改，`app_hook_read` 给应用快照，`app_hook_reply` 复制 client/session/word/callback 后入后台队列，网络不阻塞 UI。回传被 device link 队列接受或 HTTP 返回 2xx 后才提交 dismiss；失败保留通知并输出错误，不把 queued 当作主机已执行。

## 唤醒、时钟与存储

**唤醒走闹钟 API**：不在屏幕上的应用也能让设备打开它，办法是在框架的全局闹钟注册表里登记一条，见下面的“闹钟 API”。descriptor 没有 `wake` 回调了（也就没有哪个应用自己去轮询时钟）。

**墙上时钟**：设备没有电池时钟，`app_time_read(&t)` 返回本地时间（`valid`、`epoch`、`tz_min`、年月日、时分秒、星期），未校时 `valid` 为 false，应用必须处理这种情况。校时来源是 PC 的 USB `time` 操作（`AppLink.set_time()` / `python run.py time`）和联网后的 SNTP；`app_time_set()` 只由 USB 任务调用。换算在 `framework/core/app_time.c`，是纯函数，主机测试 `./scripts/test-app-time.sh`。

**最高分**：`framework/include/esphook/app_best.h`。每个应用一个 32 位记录，按应用自己的 id 存（`app_storage` 里以 `b_` 开头的键，应用自己的键不要用这个前缀），所以不用起名、也碰不到别人的。进入时 `core.best = app_best_get(ctx)`（没记录是 0），一局结束（结算画面）时 `app_best_record(ctx, core.best)`：它拿 RAM 副本比较，只有比已存的更高才写一次 flash，因此结算画面每帧调用也安全，重开、没破纪录都不写；返回 true 表示破了纪录。写失败（存储满或损坏）时这次开机内仍保留新记录，不会每帧重试。**只在一局结束时记录**，不要在得分每涨一次时记（打一个敌人写一次 flash）。小恐龙、滑雪、黄金矿工、跳一跳、僵尸围城、星际战机、极速赛车已接入；退出再进、重启后标题画面的“最高”都是上次的记录。主机测试 `./scripts/test-app-best.sh`，无屏测试 `check_records` 覆盖七个游戏的退出再进。

**设置存储**：`app_storage_load/save(key, data, size)` 给每个应用几个字节的跨重启设置：key 是 1..15 个 `[a-z0-9_]`，size 至多 `APP_STORAGE_MAX`（64 字节），读取失败（没存过或大小不符）不改动调用者的缓冲区。写 flash 慢：只在玩家明确修改时保存，不要每帧写；数据格式要带版本号和范围校验（读到别人或旧版本的数据就用默认值，闹钟的 `al_config_valid()` 是例子）。

## 闹钟 API（全局注册 + 唤醒）

`framework/include/esphook/app_alarm.h`。**日程属于框架，应用只登记条目、显示界面、回答响铃**：闹钟应用自己也是这样用的，它只剩界面和两条条目（闹钟、整点报时）。游戏和其他应用可以用同一套接口做定时提醒（水族箱 30 分钟没喂鱼、钓鱼冷却结束、每天固定时间的签到……），不需要自己的轮询，也不需要往中央运行器里加任何特判。

**登记**：`app_alarm_add(&spec)` 返回条目 id（1..255，失败为 0：参数不合法或 `APP_ALARM_SLOTS`=8 个槽用完）。`app_alarm_spec_t`：

| 字段 | 含义 |
| --- | --- |
| `owner` | 登记者，应用 id（`[a-z0-9_]`，≤23 字符）；只有登记者能 `update` |
| `target` | 到点要打开的应用，缺省等于 `owner`；可以指向别的应用（例如 `"alarm"`，借用它的响铃画面） |
| `label` | ≤15 字节，随 `APP_EVENT_ALARM` 的 `text` 送给目标，字要在字库里 |
| `kind` | `APP_ALARM_CLOCK`：每个匹配的本地时间（`minute` + `hour_mask` + `day_mask`，如 `APP_ALARM_EVERY_DAY`/`APP_ALARM_WEEKDAYS`/`APP_ALARM_EVERY_HOUR`），需要已校时；`APP_ALARM_AT`：绝对时刻 `epoch`，需要已校时；`APP_ALARM_AFTER`：登记后 `after_s` 秒，按设备运行时间计，**不需要校时** |
| `enabled` / `once` | 开关；`once` 的 CLOCK 条目响过就自己关掉（所有者要同步自己的设置） |
| `idle_only` | 只在设备停在待机应用时才响（整点报时），别的画面上不打扰，也不补响 |
| `window_s` | 到点后多久之内还算数（默认 60，CLOCK 至多 60，整点报时 15）；之后算错过，`app_alarm_stats` 计 missed，不补响 |
| `max_snoozes` / `snooze_s` | 最多贪睡几次（0 = 不可贪睡）、每次多久（默认 300 秒，按运行时间计，不受改时间影响） |
| `tag` | 应用自己用的标记，登记两条以上时用来区分是哪一条 |

**唤醒**：UI 任务每 `APP_ALARM_POLL_MS`（250ms）看一次（时钟来自 `app_platform_t.time_read`，不提供时只有 AFTER 条目和贪睡到点的条目会响），有条目到点就打开 `target`：无论当前是菜单、设置还是正在玩的游戏都会被打开（只有 `idle_only` 条目让位）；目标已经在屏幕上就不重新进入；目标不在编目里就悄悄丢弃。目标**完成进入**之后，框架给它发 `APP_EVENT_ALARM`（`key` = 条目 id，`text` = label）；没有 `on_event` 的目标只会被打开，条目随即结束。一次只响一条，其余排队等它结束。

**回答**：`app_alarm_snooze(id)`（到 `snooze_s` 后再响一次；没有次数了返回 false 且这次响铃结束）、`app_alarm_done(id)`（结束：CLOCK 条目等下一次，AT/AFTER 条目消失）。**离开目标应用等于 done**，120 秒没人回答也自动结束（`stats.timeouts`）。`app_alarm_skip(id)` 让一条 CLOCK 条目放弃此刻这一分钟（整点正好有闹钟，就让整点报时让路）。

**其他**：`app_alarm_update(id, &spec)` 说同样的话什么也不改变（所以可以在任何编辑后把全部设置再推一遍；正在响或贪睡的不会被打断），真的改了才结束响铃/贪睡；`app_alarm_cancel(id)` / `app_alarm_cancel_owner(owner)`；`app_alarm_get/list` 读回；`app_alarm_seconds_until(id, &now, ctx->now_ms)` 给“还有多久”的提示；`app_alarm_stats` 给 fired/missed/timeouts。

**约束**：
- 注册表只在 UI 任务和 `init()` 里调用（没有锁）。**USB `command` 回调不能直接碰它**，要像其他 UI 状态一样先复制入队，由 UI 任务处理。
- 注册表在 RAM 里，重启即空：应用在 `init()` 里从自己的 `app_storage` 重新登记（`init()` 要能重复调用：先 `app_alarm_cancel_owner(自己的 id)`），id 跨重启不稳定。
- 闹钟只负责“打开 + 通知”，**不会自己动舵机**：舵机仍然只能由玩家明确启用（闹钟应用的“舵机提醒”持久开关只作用于它自己的两条条目，不作用于别人借用它的闹钟）。
- 要响铃的是目标应用：它收到 `APP_EVENT_ALARM` 后画自己的界面，用 `app_idle(ctx)` 回待机，自己设定是否贪睡；不想自己做界面就把 `target` 设成 `"alarm"`，用它的铃铛画面（label 当标题，`max_snoozes` 为 0 时提示“按键关闭”）。

```c
/* 水族箱：30 分钟没人喂鱼就提醒；init() 里登记，可重复调用 */
static unsigned s_feed;
static bool init(void)
{
    (void)app_alarm_cancel_owner("aquarium");
    app_alarm_spec_t spec = {.owner = "aquarium", .label = "该喂鱼了", .kind = APP_ALARM_AFTER,
                             .after_s = 1800, .enabled = true, .max_snoozes = 2, .snooze_s = 600};
    s_feed = app_alarm_add(&spec);
    return true;
}
/* on_event：被打开之后收到通知，画自己的界面，按键回答 */
if (e->kind == APP_EVENT_ALARM) { show_hungry(ctx, e->text); return; }
if (e->kind == APP_EVENT_SHORT && ringing) { (void)app_alarm_snooze(s_feed); app_idle(ctx); }

/* 借用闹钟应用的响铃画面：其他应用的一次性提醒 */
app_alarm_spec_t once = {.owner = "fishing", .target = "alarm", .label = "鱼汛到了",
                         .kind = APP_ALARM_AFTER, .after_s = 120, .enabled = true};
(void)app_alarm_add(&once);
```

主机测试：`./scripts/test-app-alarm.sh`（注册表本身：校验、槽位与所有者、各类条目、窗口与错过、贪睡、待机限定、排队、超时、离开）、`tests/test_app_framework.c`（经管理器真实打开应用并通知）、`./scripts/test-alarm.sh` 与无屏 UI 测试（闹钟应用、其他应用登记的闹钟）。

## 内置应用与设置

| 应用 | 实现 |
| --- | --- |
| 表情 | 自动眼睛动画、自定义表情/文字、人体方向状态 |
| 投篮 | 设备端纯 C 规则，两分钟、随机速度、计分；共享画布绘制竞技场、篮网、火花，见 [robot-basketball.md](robot-basketball.md) |
| 小恐龙 | 单键跳跃跑酷；轻点即可越过地面障碍；纯 C 固定步长规则，共享画布点阵绘制（昼夜、视差、尘土），见 [robot-dino.md](robot-dino.md) |
| 滑雪大冒险 | 横版下坡，单键起跳、空中按住连续翻转、翻圈加分；纯 C 规则，共享画布绘制，见 [robot-ski.md](robot-ski.md) |
| 新婚快乐 | 默认待机：金色“新婚快乐/Happy Wedding”交替、彩带、礼花、彩纸，滚动祝福语可由 PC 消息改写，见 [robot-wedding.md](robot-wedding.md) |
| 电子木鱼 | 按键敲击，功德+1，木槌/涟漪动画，满 108 下功德圆满，长按自动敲；纯 C 计数核心，见 [robot-muyu.md](robot-muyu.md) |
| 水族箱 | 按键投一撮鱼食：鱼群追食、吃饱、气泡、水草、光柱；纯 C 模拟核心，见 [robot-aquarium.md](robot-aquarium.md) |
| 钓鱼 | 抛竿、等鱼咬钩，按提示的时机/时长按键拉鱼，鱼越大需要的次数越多、舵机摇得越凶；纯 C 规则，见 [robot-fishing.md](robot-fishing.md) |
| 星际战机 | 第一人称太空射击，雷达左右转向，按键开炮，被击中掉血并摇舵机，共 3 滴血，见 [robot-starship.md](robot-starship.md) |
| 极速赛车 | 第一人称伪 3D 公路赛，按住加速，转弯时舵机跟着转，氮气定时充满，见 [robot-racing.md](robot-racing.md) |
| 黄金矿工 | 单键放钩，钩金块/钻石/神秘袋，重物可以扔炸药，限时达到目标金额过关，最多 20 关；纯 C 核心，见 [robot-miner.md](robot-miner.md) |
| 跳一跳 | 等距方块，按住蓄力、松开起跳，落中心加倍并连击；纯 C 核心，保留三击退出，见 [robot-jump.md](robot-jump.md) |
| 僵尸围城 | 第一人称打僵尸，雷达（或 K2/K3）转向，按住开火，被咬掉血并轻摇舵机，3 滴血；纯 C 核心，见 [robot-zombie.md](robot-zombie.md) |
| 幸运签 | 按住摇签筒、松手出签：事业/人际/家庭/学业/爱情里随机一个，大吉/中吉/小吉，幸运数和幸运色；纯 C 核心，见 [robot-fortune.md](robot-fortune.md) |
| 闹钟 | 七段数码管时钟 + 闹钟 + 整点报时；日程在框架的闹钟注册表里（其他应用也能登记），本应用只提供界面并回答响铃，可贪睡，设置里可开舵机提醒；需要 USB/Wi-Fi 校时，见 [robot-alarm.md](robot-alarm.md) |
| 副屏 | 把电脑上的某个窗口或屏幕区域抓取后经 USB 投到小屏，条带 + 16 色调色板 + CRC，逐条确认，PC 端 `run.py mirror`；见 [robot-mirror.md](robot-mirror.md) |
| 做 T | PC 行情/均线/交易/结算；设备端 RAM 数据、图表和按键协议 |
| 雷达 | 三目标扇形调试图，服务快照 |
| Hook | 通知展示、未读信息、选择回复词及确认回传 |
| 按键 | USB `app_event` 回传词；复用 Hook 回传服务 |
| 设置 | 六页：固件版本/构建时间、应用数量、Hook 信息、待机设置、硬件状态、硬件配置 |
| 示例 | 点击计数、清零、动画、雷达状态和 PC 自定义标题 |
| 启动器 | 隐藏的系统应用，iPod 式列表、高亮条滑动、滚动条、图标预览和长按填充 |

设置短按循环六页。硬件配置页长按编辑、短按切换、长按保存，再长按重启生效。待机页长按进入编辑，短按切换可选应用，长按保存；三击随时回菜单。固件版本取 ESP-IDF app descriptor，当前版本由 `version.txt` 管理。数量统计已编译的可见应用，默认 22 个，不含系统 launcher；不显示凭据或认证密钥。

PC 的运行入口和协议见 [app-pc-protocol.md](app-pc-protocol.md)。旧投篮命令仍可用，start 排队打开新局，stop 回菜单，status 查询最近快照；`robot menu` 通用开菜单。

## 验证

```bash
python3 scripts/check-apps.py
python3 tests/test_scaffold.py
python3 tests/test_app_sdk.py
./scripts/test-app-games.sh
./scripts/test-app-util.sh
./scripts/test-robot.sh
./scripts/test-hardware.sh
./scripts/test-usb-cdc.sh
python3 apps/key_report/pc/test_bridge.py
./scripts/test-basketball.sh
./scripts/test-dino.sh
./scripts/test-ski.sh
./scripts/test-muyu.sh
./scripts/test-haptic-steer.sh
./scripts/test-aquarium.sh
./scripts/test-fishing.sh
./scripts/test-starship.sh
./scripts/test-racing.sh
./scripts/test-app-time.sh
./scripts/test-fortune.sh
./scripts/test-app-alarm.sh
./scripts/test-alarm.sh
./scripts/test-miner.sh
./scripts/test-jump.sh
./scripts/test-zombie.sh
./scripts/test-mirror.sh
./scripts/test-robot-ui.sh
python3 apps/mirror/pc/test_mirror.py   # 需要 Pillow
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

它使用现有 fontdata 和 64KB LVGL 堆，遍历所有 manifest 应用、重复切换、检查根节点释放、设置持久化回调与 PC 消息，并实际玩一遍投篮（出手、命中/未进、时间到结算）、小恐龙和滑雪（起跳/翻转、落地、结束、重开）、电子木鱼（敲击、浮字、自动、第 108 下）、新婚快乐（两种横幅、礼花、祝福语消息）、黄金矿工、跳一跳、僵尸围城、幸运签、闹钟（含框架闹钟注册表打开应用、其他应用登记的闹钟）和副屏（用 USB 命令喂入条带、坏行、冻结、中断、断开）；每次截图前检查屏幕上每个 label 的每个字都在固件字库里（缺字会显示成空白），并断言 LVGL 堆占用低于 80%（可设 `ESPHOOK_HEAP_REPORT=1` 打印各应用占用）。输出 PPM 屏幕帧。CI 上传这些帧，额外构建 ESP-IDF 5.5.4 默认通用配置与禁用机器人驱动的旧 Hook 配置。可选应用的完整硬件行为仍需要实机确认；主机 UI 测试不会驱动舵机，也不能证明实际 FPS。

`APP_UI_BUILD_DIR` / `APP_UI_FRAMES_DIR` 可覆盖无屏测试目录。CI 的固件尺寸检查沿用两个 OTA 槽；先看实际体积再决定是否取消 OTA。未新增固件图片或大型游戏依赖。
