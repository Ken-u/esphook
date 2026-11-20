---
name: esphook-hook
description: Set up the esphook Agent reminder screen - start the host daemon, give the board Wi-Fi and pair it, install or remove Claude Code, Codex, Kimi Code and Cursor Agent hooks, send test notifications, and debug Hook delivery. Use when a task is about Wi-Fi, daemon, pairing or Agent Hook notifications.
---

# 配置 Agent Hook 提醒屏

Hook 是设备上的一个应用：Agent 的事件（完成 `done`、错误 `error`、确认 `confirm`）经主机 daemon 转给板子，屏幕用颜色和大字提醒。不用 Hook 不影响其他应用。完整说明在 `docs/hook-setup.md`，协议在 `docs/connection-design.md`、`docs/agent-hooks.md`；代码在 `apps/hook/pc`（`host/` 只是兼容旧路径的薄转发，**不要在那里加实现**）。入口命令是 `./bin/esphook`（已安装的用户是 `~/.local/bin/esphook`）。

```text
Agent 的 Hook ──▶ 主机 daemon ◀── device link（TCP 18765，板子主动连）──▶ ESP32-C3 屏
                  管理网页 8787
```

## 动手之前先问清

- **Wi-Fi 名称和密码只能由用户提供**，不要猜，不要写进仓库、日志、提交或回答；命令里的密码会进 shell 历史，提醒用户必要时自己清理，或者让用户在本机终端里执行 `provision` 那一行。
- 板子不一定和电脑在同一网段。`--server-host` 必须是**板子网络里能访问到的主机 IP**，不能是 `127.0.0.1` 或 `0.0.0.0`。
- 安装 Hook 会改用户的全局 Agent 配置（`~/.claude/settings.json`、`~/.codex/hooks.json`、`~/.kimi-code/config.toml`、`~/.cursor/hooks.json`）。**先让用户确认要装哪些工具**，用 `--tools` 显式指定，不要默认 `all`；已有配置会留 `.esphook.bak`，卸载用 `--uninstall`。

## 1. 启动 daemon

```bash
./bin/esphook daemon --host 0.0.0.0     # 管理网页要给局域网访问时；只本机用可省略 --host
```

管理网页 `http://127.0.0.1:8787/`，device link 监听 TCP 18765，未配对设备还监听 UDP 18766 的 LAN 配对。daemon 是前台长驻进程，在 Agent 里要放到后台终端，并记下怎么停。

## 2. 给板子配网并配对

```bash
./bin/esphook provision '<Wi-Fi SSID>' '<Wi-Fi 密码>' --server-host '<主机在板子网络中的 IP>'   # 首次，需要 USB，会生成密钥并登记
./bin/esphook pair --server '<daemon 主机 IP[:18765]>'          # 板子已有 Wi-Fi：LAN 广播配对，不用串口
./bin/esphook pair --server '<IP[:18765]>' --esp                # 主机能直接访问板子（配置里要有 DEVICE_IP）：HTTP 直连
./bin/esphook status
```

- 重新配对已有设备时，主机必须保留原来的设备 Token（`~/.config/esphook/devices.json`）；丢了就先恢复出厂（**需要用户同意**）再重新配网。
- 用 USB 文本命令也能手工写：`ssid:` `pass:` `daemon_host:` `daemon_port:` `daemon_secret:<64 位十六进制>` 然后 `connect`、`status`。用 `./run.sh device cmd "..."` 发，密钥和密码不要出现在回答里。
- 板子联上以后，`./run.sh device cmd status` 的 `wifi=connected ip=... link=connected` 表示配对成功。

## 3. 安装 Agent Hook

```bash
./scripts/install-hooks.sh --tools claude           # 或 claude,codex / kimi,cursor；等价于 ./bin/esphook setup hooks --tools ...
./scripts/install-hooks.sh --tools claude --uninstall
```

一键安装（下载主机文件+固件、可选装 Hook）见 `docs/quick-install.md`；自动化环境才用 `--yes`。Hook 是 fail-open 的：daemon 或设备离线不会阻塞 Agent。会话 ID 的优先级：`--session-id` > `ESPHOOK_SESSION_ID` > `CODEX_THREAD_ID` > 配置文件。

## 4. 验证

```bash
./bin/esphook notify done "构建完成" "固件已生成"
./bin/esphook notify error "构建失败" "查看日志"
./bin/esphook notify confirm "需要确认" "是否继续"
./bin/esphook dismiss --session-id <会话 ID>
```

屏幕应分别出现绿、红、黄。三个按键默认回传 `ok`/`continue`/`done`（`./run.sh input keymap` 可改）。回键注入由主机侧显式配置，见 `docs/quick-install.md`。主机侧测试：

```bash
python3 apps/hook/pc/test_hooks.py
python3 apps/hook/pc/test_device_link.py
python3 tools/net_check.py --device <板子 IP>     # 网络路径：/health、/notify、回键 /keyevent；已配对的板子用环境变量 ESPHOOK_TOKEN
```

## 常见问题

| 现象 | 处理 |
| --- | --- |
| `notify` 成功但屏幕没反应 | 板子没打开 Hook 应用或不在待机：`./run.sh device status` 看 `active`；`status` 里 `link=offline` 说明没连上 daemon |
| `link=offline` | 板子和 daemon 不在同一网络，或 `--server-host` 填成了回环/不可达地址，或防火墙挡了 TCP 18765 |
| 配对后又失效 | 主机的设备注册表丢了（Token 不匹配）：恢复出厂并重新 `provision`（先问用户） |
| Hook 装了但没触发 | 对应 Agent 要重启；检查它的配置文件里是否有 esphook 条目 |
| 不想再用 | `./scripts/install-hooks.sh --tools all --uninstall`；板子上 Hook 应用保留，不影响其他应用 |
