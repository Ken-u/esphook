---
name: esphook-pc
description: Build PC companions for esphook apps using the shared USB AppLink SDK and bounded message protocol. Use for PC data preparation, app event consumers, custom text/control protocols, and moving expensive game logic off ESP32 flash.
---

# 开发 PC 配套

先阅读 `docs/app-pc-protocol.md`，再看 `apps/demo/pc/main.py` 和 `framework/pc/esphook_sdk/link.py`。股票回放还可参考 `apps/stock/pc` 的数据、纯逻辑和传输分层。

1. 把配套程序放在 `apps/<id>/pc`，共用传输放在 `framework/pc/esphook_sdk`；不要在 `host/` 添加新实现。可用 `scripts/new-app.py --pc` 生成起点。
2. 用 `AppLink(port)` 自动检测单个 Espressif 串口，复用逐行 JSON、seq/ACK、混合日志过滤和事件缓冲。只有运行真实 USB 会话才需要 pyserial；离线数据和规则测试用标准库。
3. 先 `catalog()` 检查 id，再 `open(id)`、`wait_active(id)` 后 `message(id, data)`。消息 JSON 对象最多 128 字节，超限要设计分块上传、完整提交和 RAM 上限，不能静默截断。小数据复用通用协议，大数据参考 stock 的暂存区原子提交。
4. 用 `poll()` 消费 `app_event`。短按、确认和全局导航仍由设备判定；PC 不另写相互冲突的手势。离开应用时暂停/取消未完成动作，关闭端口或异常时清理会话。导航 ACK 的 queued 表示已入队，不表示设置已持久化或目标已显示。
5. USB 输入使用同一个 AppLink/UsbLink/GameLink 的 tap/hold/triple/key_down/key_up；手动按住须持续 poll 续期（租期 350ms），退出/失焦 release_keys 取消。PC 键名 set_bindings 与设备 set_keymap 分开，后者保存实体/USB/回复词到 NVS；不要另开串口。hardware/set_hardware_profile 查询状态、保存配置，可显式重启。参考 docs/release-kit.md 和 apps/key_report/pc/controller.py。
6. 将网络请求、缓存、均线等计算、数据预处理、复杂游戏规则放 PC；设备保留离线交互、有限状态和绘制。提供 `--dry-run` 或离线数据路径，错误明确报出，不把演示数据当真实数据。
7. 运行 PC 纯逻辑测试、串口分包/超时/ACK 测试和无设备 dry-run。修改共享 SDK 后同时回归 `apps/stock/pc/test_stock_t.py` 与 `tests/test_app_sdk.py`。

程序消费按键事件时默认打印或处理自身状态；不要默认向其他程序注入按键。需要键盘注入时沿用 Hook 应用已有的显式配置行为。
