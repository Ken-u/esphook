# PC 配套与通用应用协议 v1

## 目录和运行

| 配套 | 路径 / 入口 |
| --- | --- |
| Hook daemon、安装器和 Agent adapters | `apps/hook/pc`；`bin/esphook`、`scripts/install-hooks.sh` 保留 |
| 做 T 数据、逻辑和 chart 协议适配 | `apps/stock/pc`；`scripts/stock-t.sh` 保留 |
| 示例程序 | `uv run --with pyserial python apps/demo/pc/main.py --text '你好'` |
| 按键事件查看器 | `uv run --with pyserial python apps/key_report/pc/main.py` |
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
