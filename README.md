# esphook

ESP32-C3 小屏应用与小游戏框架，所有内置应用共用生命周期、LVGL 显示、全局按键及设备服务。

- [应用框架与开发指南](docs/app-framework.md)：`python3 scripts/new-app.py counter --title '计数' --pc` 一步生成应用。
- [PC SDK 与协议](docs/app-pc-protocol.md)：每个应用的配套位于 `apps/<id>/pc`，共用 USB SDK 位于 `framework/pc`。
- [安装运行环境（推荐 uv）](docs/usb-environment.md)：独立虚拟环境、固定 USB 依赖及 macOS/Linux/Windows 命令；刷现成固件无需 ESP-IDF。
- 项目内置 `$esphook-app`、`$esphook-pc`、`$esphook-check`，见 [AGENTS.md](AGENTS.md)。支持项目 skills 的工具可直接使用；其他工具读取对应 SKILL.md 即可。
- 设置应用可查看固件版本、应用数量、Hook 信息，并选择待机应用（默认 **新婚快乐**，也可选表情/Hook/雷达，保存后重启保留）。

> 通用固件同时支持原屏幕/Hook 设备和桌面机器人（ESP32-C3 SuperMini + ST7735 + LD2450 + NHY009）。[下载正式发布包](https://github.com/Ken-u/esphook/releases/latest)，烧录、USB 按键 SDK、映射和硬件状态见 [发布指南](docs/release-kit.md)。
> 构建、接线及首次 USB 校准见 [机器人调试指南](docs/robot-bringup.md)。默认 auto 不初始化 PWM；机器人需先在设置中选择 robot 并重启，通过 USB 校准后使用 `robot arm` / `robot follow` 开启。
> 新增 [表情/文字协议与扇形雷达调试页](docs/robot-display-protocol.md)：USB 输入 `robot radar` 切雷达页，`robot face` 返回；JSON 可客制表情和文字。
> [GPIO6 启动器与全局按键](docs/robot-launcher.md)：默认待机为新婚快乐彩带屏（可改成眼睛动画等），短按打开菜单；短按切换、按住 500ms 画圆确认；菜单 10s 无操作回待机；快速三击回菜单（靠连按的游戏屏蔽三击，改按住 2 秒退出）。
> [新婚快乐 / Happy Wedding](docs/robot-wedding.md)：**默认待机应用**。金色“新婚快乐”和“Happy Wedding”交替，顶部彩带飘动，彩纸飘落，左右礼花轮流射出卷曲的彩带，底部滚动祝福语（可由 PC 改写）。
> [电子木鱼](docs/robot-muyu.md)：按键敲一下，**功德+1**；木槌落下、木鱼弹动、金色涟漪，满 108 下“功德圆满”，长按自动敲。
> [水族箱](docs/robot-aquarium.md)：按键扔一撮鱼食，鱼群追食、吃饱，水草、气泡、光柱。
> [钓鱼](docs/robot-fishing.md)：抛竿等咬钩，按提示的时机/时长拉鱼，鱼越大要拉的次数越多、舵机摇得越凶；3 条命，断线则鱼跑了。
> [星际战机](docs/robot-starship.md)：第一人称太空射击，**雷达控制左右，按键开炮**，被击中舵机轻摇并掉一滴血（共 3 滴）。
> [极速赛车](docs/robot-racing.md)：第一人称公路赛，**按键是油门，雷达转向，转弯时舵机跟着转**，氮气定时充满，双击点燃。
> [投篮游戏](docs/robot-basketball.md)：从菜单进入，限时 2 分钟，篮筐随机变速变向、碰壁折返，球飞行时篮筐照常移动，要提前量；每中一球 2 分；竞技场背景、篮网摆动、命中火花和飘字、倒计时条和星级结算。
> [小恐龙](docs/robot-dino.md)：Chrome 断网小恐龙，昼夜交替的像素画面。按下起跳，**轻点就能越过所有地面障碍**，按住跳得更高；高飞翼龙站着走过，速度越来越快。
> [滑雪大冒险](docs/robot-ski.md)：横版下坡滑雪。按键起跳，**空中按住连续后空翻，翻满一圈以上有额外加分**和连击；跳过石头和冰缝，捡宝石，天色从白天渐变到夜晚。
> [黄金矿工](docs/robot-miner.md)：钩子左右摆，**按键放钩**，钩金块、钻石、神秘袋；太重就**再按一下扔炸药**，限时达到目标金额过关，最多 20 关。
> [跳一跳](docs/robot-jump.md)：等距方块，**按住蓄力、松开起跳**，落在中心加倍并连击，落空就摔下去。
> [僵尸围城](docs/robot-zombie.md)：第一人称打僵尸，**雷达控制转向，按住按键开火**，被咬掉血并轻摇舵机（共 3 滴），一波比一波多。
> [今日幸运签](docs/robot-fortune.md)：按住摇签筒、松手出签，事业/人际/家庭/学业/爱情里随机一个方面，大吉/中吉/小吉，附幸运数和幸运色。
> [闹钟 / 整点报时](docs/robot-alarm.md)：七段数码管时钟，到点**自动打开**（闹钟抢占任何画面，整点报时只在待机时），可贪睡，可选舵机摇晃提醒；`python run.py time` 校时。日程由框架的**全局闹钟 API**（`app_alarm.h`）管理，闹钟应用只是界面，其他应用和游戏也能登记定时提醒，见[应用框架](docs/app-framework.md#闹钟-api全局注册--唤醒)。
> [舵机校准](docs/robot-servocal.md)：只用 K1 校准舵机的中位、摆幅、方向和速度；**装了舵机的机器人第一次开机，在屏幕设置之后自动进入**，保存后才进待机。
> [副屏](docs/robot-mirror.md)：把电脑上的某个窗口（macOS）或屏幕区域实时投到小屏，`python run.py mirror --window Terminal`。
> [做 T 高手](docs/robot-stock-t.md)：PC 随机准备 A 股历史五分钟 K 线和 MA30/MA60，通过 USB 上传 RAM；按住暂停并确认满仓买入，短按满仓卖出，每天结束结算收益。PC 入口：`./scripts/stock-t.sh`。
> 下方保留原 Agent Hook 项目说明；两种硬件共用应用框架和可选 device link，Wi-Fi 用于 Hook 连接、配置和 OTA。

esphook 是一套“主机 Agent Hook + ESP32-C3 提醒屏”项目：把 Claude Code、Codex、Kimi Code、Cursor Agent 的事件转换成 `done`、`error`、`confirm`，在 0.96 寸 ST7735 屏上用颜色和大字提醒。

仓库现在包含主机 daemon、四种 Hook、ESP-IDF 固件、中文字体资源、屏幕预览和 CI 构建配置。

## 五分钟启动

先在编译机或开发机启动 daemon。若管理网页要被局域网内其他设备访问，绑定地址不能使用 `127.0.0.1`：

```bash
./bin/esphook daemon --host 0.0.0.0
```

默认管理网页为 `http://127.0.0.1:8787/`，ESP 反向 device link 监听 TCP `18765`，未配对设备还监听 LAN 配对 UDP `18766`。然后安装 Agent Hook：

```bash
./scripts/install-hooks.sh --tools all
```

首次给板子配网并配对（需要 USB，只用于写入 Wi-Fi）：

```bash
./bin/esphook provision '<Wi-Fi SSID>' '<Wi-Fi password>' \
  --server-host '<主机在板子网络中可达的 IP>'
```

如果板子已经配好 Wi-Fi，可以直接在局域网内配对，不再依赖串口：

```bash
./bin/esphook pair --server '<daemon 主机 IP[:18765]>'
```

默认不带 `--esp` 时使用局域网 UDP 广播配对；它会把 daemon 地址和认证密钥写入设备，不修改 Wi-Fi，设备随后主动连接 daemon。只有首次配网 `provision` 需要 Python `pyserial`。

如果主机可以直接访问 ESP，显式加 `--esp` 使用 HTTP 直连：

```bash
./bin/esphook pair --server '<daemon 主机 IP[:18765]>' --esp
```

直连目标使用配置文件中的 `DEVICE_IP`；`--esp` 本身不接参数。

如果主机无法主动访问 ESP，但两者处于同一广播域，可以省略设备 IP，使用 UDP 广播配对：

```bash
./bin/esphook pair --server '<主机在板子网络中可达的 IP[:18765]>' \
  --broadcast-address '<局域网广播地址>'
```

默认广播地址为 `255.255.255.255`，部分路由器需要改成实际的定向广播地址（例如 `192.168.31.255`）。

管理网页可查看设备、设置 client/session 别名、手动发送提醒。完整连接和认证协议见 [docs/connection-design.md](docs/connection-design.md)。

## 从 GitHub 一键安装

默认分支每次 push 通过 CI 后都会自动生成一个 Release。Release 包含主机文件、可直接用于网页/curl OTA 的应用固件、USB 完整烧录包和 `SHA256SUMS`。在开发机上可以直接运行：

```bash
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash
```

脚本会把主机文件和固件保存到 `~/.local/share/esphook`，在 `~/.local/bin/esphook` 安装命令，并先校验 SHA256。安装时会检测输入回传能力：macOS 使用系统自带的 `osascript`，Linux 检测 `ydotool`、`xdotool`、`wtype`；没有检测到时，通知仍可用，但板子按键向当前窗口输入的功能不可用，脚本会提示处理方法。下载完成后，只有你明确输入 `y` 才会继续安装 Hook。安装器会先按 `PATH` 检测 Agent 命令，只修改实际已安装的 Claude Code、Codex、Kimi Code、Cursor Agent；未检测到的工具会跳过，不会创建对应配置。已有配置会保留 `.esphook.bak` 备份。

常用选项：

```bash
# 只下载文件和固件，不安装 Hook
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash -s -- --no-hooks

# 只安装指定工具的 Hook
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash -s -- --tools claude,codex

# 明确授权给自动化环境，跳过确认提示
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash -s -- --yes
```

安装脚本需要 `curl`、`python3` 和 `tar`。详细的安装目录、Release 资产和手动烧录方式见 [docs/quick-install.md](docs/quick-install.md)。

## 主机侧 Agent Hook

安装器只会为当前 `PATH` 中检测到的 Agent 写入 Hook；未安装的工具会提示并跳过，不会创建对应配置。

安装器支持单独选择工具：

```bash
./scripts/install-hooks.sh --tools claude
./scripts/install-hooks.sh --tools claude,codex
./scripts/install-hooks.sh --tools kimi,cursor
./scripts/install-hooks.sh --tools all --uninstall
```

也可以使用：

```bash
./bin/esphook setup hooks --tools all
./bin/esphook setup codex
```

| Agent | 配置文件 | 完成 | 错误 | 确认 |
| --- | --- | --- | --- | --- |
| Claude Code | `~/.claude/settings.json` | `Stop` | `StopFailure` | `Notification` |
| Codex | `~/.codex/hooks.json` | `Stop` | Stop 状态 | `PermissionRequest`；确认后的 `PostToolUse` 自动 dismiss |
| Kimi Code | `~/.kimi-code/config.toml` | `Stop` | `StopFailure` | `PermissionRequest` / `Notification` |
| Cursor Agent | `~/.cursor/hooks.json` | `stop` | `stop.status=error` | `stop.status=aborted` |

Hook 从 stdin JSON 读取会话 ID：Claude Code、Codex、Kimi Code 使用 `session_id`，Cursor 使用 `conversation_id`。CLI 的优先级为：

```text
--session-id > ESPHOOK_SESSION_ID > CODEX_THREAD_ID > 配置文件 SESSION_ID
```

Hook 是 fail-open 的：daemon 或设备离线时不会阻塞 Agent。详细配置与事件映射见 [docs/agent-hooks.md](docs/agent-hooks.md)。

## CLI

```bash
./bin/esphook notify done "构建完成" "固件已生成"
./bin/esphook notify error "构建失败" "查看日志"
./bin/esphook notify confirm "需要确认" "是否继续"
./bin/esphook notify --session-id <session-id> done "指定会话完成"
./bin/esphook dismiss --session-id <session-id>
./bin/esphook status
```

CLI 优先调用 daemon；配置了 `DEVICE_IP` 时，daemon 不可用会退回直连 ESP HTTP。默认配置文件为 `~/bin/esphook.conf`，也可用 `ESPHOOK_CONF` 指定：

```ini
DEVICE_IP=192.168.31.180
CLIENT_ID=workstation:manual
SESSION_ID=default
DAEMON_HOST=192.168.1.203
WEB_BIND=0.0.0.0
WEB_PORT=8787
DEVICE_PORT=18765
```

## 硬件与固件

目标硬件是 4 MB Flash 的 ESP32-C3 SuperMini，加一块 0.96 寸 160×80 ST7735 SPI 屏、三个按键和一个无源蜂鸣器。接线、电平、初次 USB 烧录和分区说明见 [docs/hardware.md](docs/hardware.md)。

屏幕布局已经用 LVGL 实现：完成为绿色、错误为红色、确认是黄色；标题/正文占主要面积，过长文字自动滚动；当前框架保持待机表情动画，菜单 10s 无操作回所选待机应用。

OTA 只更新 `ota_0`/`ota_1` 应用分区，不会覆盖 NVS、分区表或固定 `fontdata` 中文字库。网页上传、curl、USB 完整刷写和 reverse device link 的限制见 [docs/ota.md](docs/ota.md)。

## CI 与构建产物

`.github/workflows/ci.yml` 在每次 push、Pull Request 和手动触发时运行主机测试，并用 Espressif 的 ESP-IDF v5.5.4 环境构建 ESP32-C3 固件。Firmware job 会上传应用 `.bin`、bootloader、分区表、`flasher_args.json` 等构建产物；另一个资源 artifact 包含 USB 初次烧录所需的 `main/fontdata.bin` 和分区表。默认分支的 push 在构建成功后还会自动发布 `v0.0.0-<run number>` Release，Release 资产可直接被一键安装脚本使用。

本地构建：

```bash
source "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
```

主机测试：

```bash
python3 -m py_compile host/esphookd.py host/install_hooks.py host/hooks/esphook_notify_hook.py
python3 apps/hook/pc/test_hooks.py
python3 apps/hook/pc/test_device_link.py
```

屏幕效果预览在 [screen-layout-preview.html](screen-layout-preview.html) 和 [screen-focus-preview.html](screen-focus-preview.html)。
