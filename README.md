# esphook

esphook 是一套“主机 Agent Hook + ESP32-C3 提醒屏”项目：把 Claude Code、Codex、Kimi Code、Cursor Agent 的事件转换成 `done`、`error`、`confirm`，在 0.96 寸 ST7735 屏上用颜色和大字提醒。

仓库现在包含主机 daemon、四种 Hook、ESP-IDF 固件、中文字体资源、屏幕预览和 CI 构建配置。

## 五分钟启动

先在编译机或开发机启动 daemon。若管理网页要被局域网内其他设备访问，绑定地址不能使用 `127.0.0.1`：

```bash
./bin/esphook daemon --host 0.0.0.0
```

默认管理网页为 `http://127.0.0.1:8787/`，ESP 反向 device link 监听 TCP `18765`。然后安装 Agent Hook：

```bash
./scripts/install-hooks.sh --tools all
```

首次给板子配网并配对（主机 daemon 要先启动）：

```bash
./bin/esphook provision '<Wi-Fi SSID>' '<Wi-Fi password>' \
  --server-host '<主机在板子网络中可达的 IP>'
```

如果板子已经配好 Wi-Fi，只需要写入 daemon 地址和认证密钥，用 `pair`。命令会通过 USB CDC 写入配置，设备随后主动连接主机；主机不需要反向访问设备所在网段。`provision`/`pair` 需要 Python `pyserial`。

管理网页可查看设备、设置 client/session 别名、手动发送提醒。完整连接和认证协议见 [docs/connection-design.md](docs/connection-design.md)。

## 主机侧 Agent Hook

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
| Codex | `~/.codex/hooks.json` | `Stop` | Stop 状态 | `PermissionRequest` |
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

屏幕布局已经用 LVGL 实现：完成为绿色、错误为红色、确认是黄色；标题/正文占主要面积，过长文字自动滚动；10 分钟没有新事件后关闭显示和背光，按键或新通知唤醒。

OTA 只更新 `ota_0`/`ota_1` 应用分区，不会覆盖 NVS、分区表或固定 `fontdata` 中文字库。网页上传、curl、USB 完整刷写和 reverse device link 的限制见 [docs/ota.md](docs/ota.md)。

## CI 与构建产物

`.github/workflows/ci.yml` 在每次 push、Pull Request 和手动触发时运行主机测试，并用 Espressif 的 ESP-IDF v5.5.4 环境构建 ESP32-C3 固件。Firmware job 会上传应用 `.bin`、bootloader、分区表、`flasher_args.json` 等构建产物；另一个资源 artifact 包含 USB 初次烧录所需的 `main/fontdata.bin` 和分区表。

本地构建：

```bash
source "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
```

主机测试：

```bash
python3 -m py_compile host/esphookd.py host/install_hooks.py host/hooks/esphook_notify_hook.py
python3 host/test_hooks.py
python3 host/test_device_link.py
```

屏幕效果预览在 [screen-layout-preview.html](screen-layout-preview.html) 和 [screen-focus-preview.html](screen-focus-preview.html)。
