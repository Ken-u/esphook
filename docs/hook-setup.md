# Agent Hook：daemon、配对与 CLI

esphook 最早是一套“主机 Agent Hook + ESP32-C3 提醒屏”：把 Claude Code、Codex、Kimi Code、Cursor Agent 的事件转成 `done`、`error`、`confirm`，在 0.96 寸 ST7735 屏上用颜色和大字提醒。现在它是设备上的 **Hook 应用**，和游戏共用同一套应用框架；不用 Hook 也不影响其他应用。

这一页是 Hook 的完整使用说明：启动 daemon、给板子配网和配对、安装 Agent Hook、命令行。固件烧录见 [README](../README.md#1-烧录固件)，接线见 [hardware.md](hardware.md)。

整体结构：

```text
Agent 的 Hook ──▶ 主机 daemon ◀── device link（TCP 18765，板子主动连）──▶ ESP32-C3 屏
                  管理网页 8787
```

## 1. 启动 daemon

在开发机上启动 daemon。管理网页要被局域网内其他设备访问时，绑定地址不能是 `127.0.0.1`：

```bash
./bin/esphook daemon --host 0.0.0.0
```

默认管理网页为 `http://127.0.0.1:8787/`，ESP 反向 device link 监听 TCP `18765`，未配对设备还监听 LAN 配对 UDP `18766`。管理网页可查看设备、设置 client/session 别名、手动发送提醒。完整连接和认证协议见 [connection-design.md](connection-design.md)。

## 2. 给板子配网并配对

首次配网（需要 USB，只用于写入 Wi-Fi，需要 Python `pyserial`）：

```bash
./bin/esphook provision '<Wi-Fi SSID>' '<Wi-Fi password>' \
  --server-host '<主机在板子网络中可达的 IP>'
```

板子已经有 Wi-Fi 时，可以直接在局域网内配对，不再依赖串口：

```bash
./bin/esphook pair --server '<daemon 主机 IP[:18765]>'
```

不带 `--esp` 时使用局域网 UDP 广播配对：它把 daemon 地址和认证密钥写入设备，不修改 Wi-Fi，设备随后主动连接 daemon。

如果主机可以直接访问 ESP，加 `--esp` 用 HTTP 直连（目标取配置文件里的 `DEVICE_IP`，`--esp` 本身不接参数）：

```bash
./bin/esphook pair --server '<daemon 主机 IP[:18765]>' --esp
```

主机无法主动访问 ESP，但两者在同一广播域时，可以省略设备 IP，用 UDP 广播配对：

```bash
./bin/esphook pair --server '<主机在板子网络中可达的 IP[:18765]>' \
  --broadcast-address '<局域网广播地址>'
```

默认广播地址是 `255.255.255.255`，部分路由器需要改成实际的定向广播地址（例如 `192.168.31.255`）。

USB CDC 上也可以直接发配置命令（`provision` 就是替你发这些，并生成密钥、登记 daemon）：

```text
ssid:<Wi-Fi 名称>
pass:<Wi-Fi 密码>
daemon_host:<主机 IP>
daemon_port:18765
daemon_secret:<64 位十六进制密钥>
connect
status
```

## 3. 一键安装（daemon、Hook、固件）

默认分支每次 push 通过 CI 后都会生成一个 Release，里面有主机文件、OTA 固件、USB 完整烧录包和 `SHA256SUMS`。在准备跑 daemon 的主机上：

```bash
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash
```

脚本把主机文件和固件保存到 `~/.local/share/esphook`，在 `~/.local/bin/esphook` 安装命令，先校验 SHA256。下载完成后，只有你明确输入 `y` 才会继续安装 Hook；它按 `PATH` 检测 Agent 命令，只修改实际已安装的 Claude Code、Codex、Kimi Code、Cursor Agent，已有配置保留 `.esphook.bak` 备份。常用选项：

```bash
# 只下载文件和固件，不安装 Hook
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash -s -- --no-hooks

# 只安装指定工具的 Hook
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash -s -- --tools claude,codex

# 明确授权给自动化环境，跳过确认提示
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash -s -- --yes
```

需要 `curl`、`python3`、`tar`。安装目录、Release 资产、按键输入回传（macOS 的 `osascript` / Linux 的 `ydotool`、`xdotool`、`wtype`）和手动烧录见 [quick-install.md](quick-install.md)。

## 4. 主机侧 Agent Hook

已经克隆了仓库时，也可以直接用仓库里的安装器（同样只为当前 `PATH` 中检测到的 Agent 写入 Hook）：

```bash
./scripts/install-hooks.sh --tools all
./scripts/install-hooks.sh --tools claude
./scripts/install-hooks.sh --tools claude,codex
./scripts/install-hooks.sh --tools kimi,cursor
./scripts/install-hooks.sh --tools all --uninstall
```

等价写法：

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

Hook 从 stdin JSON 读取会话 ID：Claude Code、Codex、Kimi Code 用 `session_id`，Cursor 用 `conversation_id`。CLI 的优先级：

```text
--session-id > ESPHOOK_SESSION_ID > CODEX_THREAD_ID > 配置文件 SESSION_ID
```

Hook 是 fail-open 的：daemon 或设备离线时不会阻塞 Agent。详细配置与事件映射见 [agent-hooks.md](agent-hooks.md)。

板子上三个按键默认映射为 K1=`ok`、K2=`continue`、K3=`done`，作为 Hook 回传词，可以在设备网页或用 `python run.py input keymap` 配置。

## 5. 命令行

```bash
./bin/esphook notify done "构建完成" "固件已生成"
./bin/esphook notify error "构建失败" "查看日志"
./bin/esphook notify confirm "需要确认" "是否继续"
./bin/esphook notify --session-id <session-id> done "指定会话完成"
./bin/esphook dismiss --session-id <session-id>
./bin/esphook status
```

CLI 优先调用 daemon；配置了 `DEVICE_IP` 时，daemon 不可用会退回直连 ESP HTTP。默认配置文件是 `~/bin/esphook.conf`，也可用 `ESPHOOK_CONF` 指定：

```ini
DEVICE_IP=192.168.31.180
CLIENT_ID=workstation:manual
SESSION_ID=default
DAEMON_HOST=192.168.1.203
WEB_BIND=0.0.0.0
WEB_PORT=8787
DEVICE_PORT=18765
```

## 6. 屏幕上的样子、OTA 和测试

- 完成是绿色、错误是红色、确认是黄色；标题和正文占主要面积，过长的文字自动滚动。静态效果预览在仓库根目录的 [screen-layout-preview.html](../screen-layout-preview.html) 和 [screen-focus-preview.html](../screen-focus-preview.html)。
- OTA 只更新 `ota_0` / `ota_1` 应用分区，不覆盖 NVS、分区表和固定 `fontdata` 中文字库。网页上传、curl、USB 完整刷写和 reverse device link 的限制见 [ota.md](ota.md)。
- 主机侧测试：

```bash
python3 -m py_compile host/esphookd.py host/install_hooks.py host/hooks/esphook_notify_hook.py
python3 apps/hook/pc/test_hooks.py
python3 apps/hook/pc/test_device_link.py
```
