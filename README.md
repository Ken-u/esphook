# esphook

主机侧 Agent 通知桥接工具。它把 Claude Code、Codex、Kimi Code、Cursor Agent 的生命周期事件转换成统一的 `done`、`error`、`confirm` 通知，再通过 `esphook daemon` 转发到提醒设备。

## 功能

- 统一 CLI：`notify`、`dismiss`、`status`
- 主机 daemon 管理网页和 HTTP API
- ESP 位于下游网络时，支持 ESP 主动连接 daemon 的反向 device link
- 每台设备使用独立 HMAC 密钥认证
- 每个请求携带 `client_id` 和 `session_id`，可同时区分多个 Agent 和多个会话
- 自动安装四种 Agent 的用户级 Hook
- Hook fail-open：daemon 或设备离线不会阻塞 Agent 工作

## 快速开始

```bash
git clone git@github.com:Ken-u/esphook.git
cd esphook

# 启动主机 daemon
./bin/esphook daemon

# 安装 Claude Code、Codex、Kimi Code、Cursor Agent Hook
./scripts/install-hooks.sh --tools all
```

需要从其他设备访问管理网页时：

```bash
./bin/esphook daemon --host 0.0.0.0
```

管理网页默认在 `http://127.0.0.1:8787/`，device link 默认监听 `18765`。

## Agent Hook

安装器支持单独选择工具：

```bash
./scripts/install-hooks.sh --tools claude
./scripts/install-hooks.sh --tools claude,codex
./scripts/install-hooks.sh --tools kimi,cursor
```

也可以使用 CLI：

```bash
./bin/esphook setup hooks --tools all
./bin/esphook setup codex
```

Hook 配置位置和事件映射：

| Agent | 配置文件 | 完成 | 错误 | 确认 |
| --- | --- | --- | --- | --- |
| Claude Code | `~/.claude/settings.json` | `Stop` | `StopFailure` | `Notification` |
| Codex | `~/.codex/hooks.json` | `Stop` | Stop 状态 | `PermissionRequest` |
| Kimi Code | `~/.kimi-code/config.toml` | `Stop` | `StopFailure` | `PermissionRequest` / `Notification` |
| Cursor Agent | `~/.cursor/hooks.json` | `stop` | `stop.status=error` | `stop.status=aborted` |

Hook 适配器从 stdin JSON 读取原生会话 ID：Claude Code、Codex、Kimi Code 使用 `session_id`，Cursor Agent 使用 `conversation_id`。随后每次调用 daemon 都显式发送该 ID。

会话 ID 的 CLI 优先级是：

```text
--session-id > ESPHOOK_SESSION_ID > CODEX_THREAD_ID > 配置文件 SESSION_ID
```

卸载自己安装的 Hook：

```bash
./scripts/install-hooks.sh --tools all --uninstall
```

安装器保留已有配置，重复执行不会增加重复项；首次修改已有配置前会创建同目录下的 `.esphook.bak`。Codex 可能需要在 `/hooks` 中审核并信任新 Hook。

详细说明见 [docs/agent-hooks.md](docs/agent-hooks.md)。

## CLI

```bash
./bin/esphook notify done "构建完成" "固件已生成"
./bin/esphook notify error "构建失败" "查看日志"
./bin/esphook notify confirm "需要确认" "是否继续"

./bin/esphook notify --session-id <session-id> done "指定会话完成"
./bin/esphook dismiss --session-id <session-id>
./bin/esphook status
```

CLI 会优先调用本机 daemon；daemon 不可用时，如果配置了 ESP 地址，则尝试直连 ESP HTTP 接口。

## 配置

默认读取 `~/bin/esphook.conf`，也可以通过 `ESPHOOK_CONF` 指定配置文件：

```bash
DEVICE_IP=192.168.31.180
CLIENT_ID=workstation:manual
SESSION_ID=default
DAEMON_PORT=18765
```

daemon 管理网页相关配置：

```bash
DAEMON_HOST=192.168.1.203
WEB_BIND=0.0.0.0
WEB_PORT=8787
DEVICE_PORT=18765
```

管理网页提供 client/session alias。alias 只用于显示和路由层映射，配置文件默认位于 `~/.config/esphook/aliases.json`。

## 设备连接模式

```text
Agent / CLI / 浏览器
          │ HTTP
          ▼
   esphook daemon
      │       ▲
      │       │ ESP 主动 device link
      ▼       │
      ESP32-C3
```

主机可以访问 ESP 时使用直连 HTTP；ESP 位于下游 NAT 时，ESP 主动连接主机的 device link，daemon 自动通过反向连接发送通知。详细协议见 [docs/connection-design.md](docs/connection-design.md)。

## 开发与测试

```bash
python3 -m py_compile host/esphookd.py host/install_hooks.py host/hooks/esphook_notify_hook.py
python3 host/test_hooks.py
python3 host/test_device_link.py
```

本仓库提交的是主机侧 esphook 和 Agent Hook。ESP 固件、屏幕布局预览和字体资源仍在硬件工程工作树中单独维护。
