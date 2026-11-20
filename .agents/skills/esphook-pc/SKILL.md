---
name: esphook-pc
description: Build PC companions for esphook apps using the shared USB AppLink SDK and bounded message protocol. Use for PC data preparation, app event consumers, custom text/control protocols, scripts that drive a board, and moving expensive game logic off ESP32 flash.
---

# 开发 PC 配套

先读 `docs/app-pc-protocol.md`，再看 `apps/demo/pc/main.py` 和 `framework/pc/esphook_sdk/link.py`。股票回放可参考 `apps/stock/pc` 的数据、纯逻辑和传输分层。只想看板子状态、开应用、发一条文本命令时不用写代码，用 `python3 run.py device ...`（见 `esphook-device`）。

## 放在哪里、怎么起步

- 配套程序放 `apps/<id>/pc`，共用传输放 `framework/pc/esphook_sdk`；**不要在 `host/` 添加新实现**。`scripts/new-app.py --pc` 生成起点。
- 把网络请求、缓存、均线等计算、数据预处理、复杂游戏规则放 PC；设备保留离线交互、有限状态和绘制。提供 `--dry-run` 或离线数据路径，错误明确报出，不把演示数据当真实数据。
- 在 `run.py` 里接入新命令时，需要额外依赖（例如 Pillow）用 `tools/bootstrap.ensure(extra, optional)` 声明。

## 连接与消息

1. 用 `AppLink(port)` 自动检测单个 Espressif 串口，复用逐行 JSON、seq/ACK、混合日志过滤和事件缓冲。需要自己打开串口时用 `esphook_sdk.link.open_serial()`：保持 DTR、先清 RTS，否则 USB Serial/JTAG 会把 `dtr = rts = False` 当作复位序列，每次连接重启设备。只有运行真实 USB 会话才需要 pyserial；离线数据和规则测试用标准库。
2. 先 `catalog()` 检查 id，再 `open(id)`、`wait_active(id)` 后 `message(id, data)`。消息 JSON 对象最多 128 字节；超限要设计分块上传、完整提交和 RAM 上限，不能静默截断。小数据复用通用协议，大数据参考 stock 的暂存区原子提交。
3. 应用自己的行协议（如副屏的 `mirror ...`）用 `UsbLink.send_line()` 写行、`wait_message(type, match, timeout)` 等回复，自己负责确认、重试和差分，每行 ≤1024 字节；参考 `apps/mirror/pc`。
4. 用 `poll()` 消费 `app_event`。短按、确认和全局导航仍由设备判定，PC 不另写相互冲突的手势。离开应用时暂停或取消未完成动作，关闭端口或异常时清理会话。导航 ACK 的 `queued` 只表示已入队，不表示设置已持久化或目标已显示。

## 键盘、映射与硬件配置

- USB 输入用同一个 AppLink/UsbLink/GameLink 的 `tap/hold/triple/key_down/key_up`；手动按住须持续 `poll()` 续期（租期 350 ms），退出或失焦 `release_keys()` 取消。
- PC 键名 `set_bindings` 和设备 `set_keymap` 是两回事，后者把实体键/USB 键/回复词保存到 NVS；不要另开串口。`hardware()` / `set_hardware_profile()` 查询状态、保存配置，可显式重启。
- 参考：`docs/release-kit.md`、`apps/key_report/pc/controller.py`（Tk 窗口）和 `bridge.py`（默认空格 = K1、← = K2、→ = K3 的持续键盘转发；K2/K3 点按延长到 200 ms 才转得动；有 pynput 用全局真实按下/松开，否则退回终端短按；复用同一连接并打印设备事件）。
- 程序消费按键事件时默认打印或处理自身状态，不要默认向其他程序注入按键；需要键盘注入时沿用 Hook 应用已有的显式配置行为。

## 验证

运行 PC 纯逻辑测试、串口分包/超时/ACK 测试和无设备 `--dry-run`。修改共享 SDK 后同时回归 `apps/stock/pc/test_stock_t.py` 与 `tests/test_app_sdk.py`；整套用 `./scripts/check-all.sh`。
