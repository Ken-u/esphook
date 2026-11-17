# PC 配套与通用应用协议 v1

## 目录和运行

| 配套 | 路径 / 入口 |
| --- | --- |
| Hook daemon、安装器和 Agent adapters | `apps/hook/pc`；`bin/esphook`、`scripts/install-hooks.sh` 保留 |
| 做 T 数据、逻辑和 chart 协议适配 | `apps/stock/pc`；`scripts/stock-t.sh` 保留 |
| 示例程序 | `uv run --with pyserial python apps/demo/pc/main.py --text '你好'` |
| 按键事件查看器 | `uv run --with pyserial python apps/key_report/pc/main.py` |
| USB 键盘窗口 | `python run.py keys --config apps/key_report/pc/bindings.example.json` |
| 实时键盘转发 | `uv run apps/key_report/pc/bridge.py`（默认空格 = K1，全局监听，按住即设备按住）或 `python run.py bridge` |
| 通用 USB SDK | `framework/pc/esphook_sdk`，AppLink / UsbLink |

没有 uv 时安装 pyserial 后用 Python 3。传 `--port` 指定串口，否则匹配唯一 Espressif USB；同一串口不要同时运行 monitor 和配套。demo / key_report / stock 支持 `--dry-run`，不会打开 USB。

SDK 将框架路径加入 Python 导入目录，示例见各应用 pc/main.py。自定义协议适配器继承 UsbLink 并设置 prefix/ack_type；股票 GameLink 只在 stock 应用中，通用 SDK 不含行情和交易知识。

## AppLink

```python
from esphook_sdk import AppLink
link = AppLink(port=None)
try:
    print(link.catalog())
    link.open("demo")
    link.wait_active("demo")
    link.message("demo", {"text": "Hello"})
    for event in link.poll():
        print(event)
finally:
    link.close()
```

设备负责菜单、消抖与手势。PC 负责网络、数据缓存、重计算和适合放 PC 的规则；应用退出时通过 on_leave 的 app_event 通知自己的 PC 状态。当前 stock 保留其 game_button menu/launch 机制。通用 SDK 不自动推断暂停策略，PC 应用按自身事件和 status 实现。

## 下行

一行 `app ` + UTF-8 JSON + 换行，JSON ≤1024 字节，嵌套 ≤8，拒绝重复顶层字段、尾随非空数据和内嵌 NUL。必填 `v:1`、`seq` 正整数（最大 2147483647，SDK 回绕）、op。应用 id 1..23 位，名称匹配 manifest。

| op | 例子 / 响应 |
| --- | --- |
| list | `{"v":1,"seq":1,"op":"list"}`；apps 返回 id/title/standby，排除隐藏应用 |
| status | 当前 active、standby、api |
| open | `{"v":1,"seq":2,"op":"open","app":"demo"}` |
| menu | 排队回系统启动器 |
| standby | 带 app，必须有 APP_STANDBY；排队保存 NVS |
| key | 带 key（0..2 整数）、pressed（布尔）；原始按下/松开入队，由设备判定手势 |
| keys_release | 取消所有 USB 按住，不产生短按或确认 |
| keymap | 无 mapping 时读取；写入完整 `{"physical":[0,1,2],"usb":[0,1,2],"words":["ok","continue","done"]}`；同步保存 NVS |
| hardware | 无 profile 时读取；可选 profile 为 auto/screen/robot，可选 reboot 布尔需同时带 profile |
| message | 带 app 和 data 对象；data 序列化后 ≤128 UTF-8 字节，只送当前前台目标应用 |

返回 `{"v":1,"type":"app_ack","seq":2,"ok":true,"queued":true}`。seq 匹配请求；`queued` 只保证操作已入队，open 后使用 wait_active/status 验证进入。standby 保存结果通过后续 status 的 standby 确认，保存失败保留旧值并输出错误。message 若在消费前离开目标页面会被丢弃，不跨应用保留私有消息。

菜单/设置不会暴露密钥。此 USB 协议只用于直连电脑；没有新增未经配对的 HTTP 控制入口。已有 `/api/v1/display` 及股票 `game` JSON v1 兼容。

## 上行

```json
{"v":1,"type":"app_event","app":"demo","action":"tap","key":0,"ms":12345}
```

`app_host_event` 输出 app/action/key/ms 和可选 text。key 是 0..2 的逻辑键编号，ms 是设备单调时间。事件动作由应用定义：demo 发 launch/leave/tap/reset，key_report 发 key 并附回复词。按键查看器只打印，不向其他应用注入按键。

SDK 过滤普通日志、合并分包，只接受匹配 seq 的 ACK，并在等待 ACK 时暂存事件。事件队列最多 256 项，消费者需及时 poll；过慢会丢弃最旧事件。错误/断开以异常报告，关闭端口后由应用自己的协议策略清理会话。

大数据不要塞进通用 message。参考 stock 的 begin/bars/show：校验总数、偏移、行内容，上传到暂存区，完整后一次提交；数据只在 RAM，设置才写 NVS。使用 game v1 的股票上位机仍与本版兼容，具体字段见 [robot-stock-t.md](robot-stock-t.md)。

## 回复长度与丢帧

设备的 USB TX 环为 2048 字节（`CDC_TX_BUFFER_BYTES`），整行 ACK 按 256 字节分片写入；`cdc_write` 只有整行进入 TX 环才返回成功。IDF 默认的 256 字节环放不下超过它的单次写入，曾使约 480 字节的 `list` ACK 整条丢失、线上只剩一个换行。现在：

- `scripts/check-apps.py` 按全部 manifest 的标题和标志估算 `list` ACK 的最坏长度，超过 `CDC_TX_BUFFER_BYTES` 即失败；新增应用前先看这条预算。
- 主机长时间不读串口时设备丢弃整行，不再留下孤立换行；被截断的行以换行收尾，不会与下一帧粘连。`usb_tx_drops` 是开机以来丢弃的行数，请求超时后立即查 `hardware`，若数字增长即确认设备侧 TX 停滞。
- 打开串口**不会**复位设备：USB Serial/JTAG 把「DTR 低 + RTS 高」当作复位序列，而系统打开串口时两条线都置位、pyserial 又先应用 DTR，所以旧的 `dtr = rts = False` 会让芯片在每次连接时重启（ROM 打印 `rst:0x15 USB_UART_CHIP_RESET`）。这曾让 ACK 内容随启动进度变化（`hardware` 刚启动 229 B、启动完成后 259 B，恰好跨过 256 B 的旧 TX 环），RAM 里的当前应用也被清掉。SDK 现在用 `esphook_sdk.link.open_serial()`：保持 DTR、只清 RTS。自写工具请复用它，不要自己 `dtr = rts = False`。
- 复位后的启动日志、设备残留的旧行都可能在打开串口后先读到；SDK 忽略非 JSON 行和 seq 不匹配的 ACK。

## USB 输入 SDK

```python
link.set_bindings({"space": 0, "return": 0})  # PC 键名映射到 USB 原始键
link.tap("space")
link.hold(0, 550)
link.triple(0)
print(link.keymap())
link.set_keymap(physical=[0,1,2], usb=[0,1,2], words=["ok","continue","done"])
print(link.hardware())
link.set_hardware_profile("robot", reboot=True)
```

`bridge.py`（`run.py bridge`）是同一 SDK 的持续键盘转发：监听 PC 键盘并转成 key_down/key_up，同时把设备事件（key_report、应用事件）按行 JSON 打印。默认只绑定空格 = K1：按下/按住/连按三次分别是设备上的短按/长按确认/全局三击，手势仍由设备判定。`--backend auto`（默认）在装有 pynput 时用全局监听，得到真实的按下/松开（按住期间 SDK 每 0.1s 续租 350ms 租约，系统自动重复被忽略），窗口是否在前台都生效，`--quit-key`（默认 F12）停止；macOS 要给终端“输入监控”权限，缺权限时启动即提示。没有 pynput 时退回 `terminal` 后端：终端拿不到松开事件，因此每个键是一次短按并过滤系统自动重复（按住不再连发，但无法变成长按），`h` 再按键是 550ms 长按，`t` 三击 K1，`m` 菜单，`r` 取消按住，`q`/Esc 退出。全局监听无法吞掉按键，空格仍会输入到当前窗口；要避免请用 `--config`（`bridge.example.json` 用 F9/F10/F11）绑定专用键。它不向其他程序注入按键，和 monitor、其他 PC 配套一样独占串口。

keymap ACK 的 mapping 是已持久化配置，usb_lease_ms 为 350。physical/usb 各为三个 0..2 的逻辑键；允许多个原始键映射到同一键。words 为三个 1..15 字符可打印 ASCII 回复词。正在按住的键使用 DOWN 时的映射直到松开。

hold/press/poll 每 100ms 续期；手动 key_down 后须持续 poll 或重复 key_down。超时发 CANCEL，释放后不补发 SHORT/CONFIRM。实体/USB 多来源按住合并，一方释放不会松开另一方。close 与窗口失焦取消 USB 键；GameLink 使用相同连接调用这些接口，不占第二个串口。stock 真机只接受设备确认，CANCEL 恢复暂停而不交易。

hardware ACK 的 hardware 含 active/configured/restart_required/drivers_compiled/physical_keys、screen/radar/servo、radar_frames、usb_tx_drops，以及 heap_free / heap_min（当前空闲堆和开机以来的最低空闲堆，字节）。状态为 unknown、initialized_connection_unknown、not_detected、detected、disabled 或 error。初始化成功不代表设备实物接入；只有有效雷达帧可确认检测。auto 默认不初始化 PWM，robot 需保存后重启并显式 arm；旧屏幕设备可选 screen。详见 [发布指南](release-kit.md)。
