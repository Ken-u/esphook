# esphook 快速安装

## 一键安装

在准备运行 daemon 的主机上执行：

```bash
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh | bash
```

脚本从 `Ken-u/esphook` 的最新正式 Release 获取四个资产：

- `esphook-host.tar.gz`：daemon、CLI、Hook 适配器和文档；
- `esphook-firmware.bin`：只包含应用分区，可用于网页或 curl OTA；
- `esphook-full-flash.zip`：包含 bootloader、分区表、OTA data、应用和中文字库；
- `SHA256SUMS`：安装前自动校验以上三个文件。

主机文件和固件默认安装到 `~/.local/share/esphook`，并在 `~/.local/bin/esphook` 创建命令入口。安装脚本不会默认修改 Agent 配置：下载和校验成功后，它会询问是否安装 Hook，只有输入 `y` 才会继续。继续安装前会按 `PATH` 检测 `claude`、`codex`、`kimi`/`kimi-code`、`cursor-agent`/`cursor`，只为已安装的 Agent 写入配置，其他工具会跳过。修改已有配置前会保留 `.esphook.bak` 备份。

脚本需要 `curl`、`python3` 和 `tar`。`curl | bash` 场景下确认提示从 `/dev/tty` 读取，因此不会因为脚本来自管道而失效。

## 不使用串口完成 LAN 配对

设备已经配置过 Wi-Fi 时，`pair` 默认使用 LAN 广播把 daemon 地址和随机认证密钥写入设备，不需要连接 USB 串口：

```bash
~/.local/bin/esphook pair \
  --server '<主机在板子网络中可达的 IP[:18765]>'
```

如果主机可以直接访问 ESP，使用 `--esp` 强制 HTTP 直连：

```bash
~/.local/bin/esphook pair \
  --server '<主机在板子网络中可达的 IP[:18765]>' --esp
```

直连目标从配置文件的 `DEVICE_IP` 读取，`--esp` 不接参数。`--server` 的端口是 daemon device-link 端口，默认 `18765`；不能填写 `127.0.0.1` 或 `0.0.0.0`。首次配网仍使用 `provision` 和 USB。

如果主机不能主动访问设备，但两者在同一广播域，可以不填设备 IP，让命令使用 UDP `18766` 广播：

```bash
~/.local/bin/esphook pair \
  --server '<主机在板子网络中可达的 IP[:18765]>' \
  --broadcast-address '<局域网广播地址>'
```

默认广播地址是 `255.255.255.255`；如果网络不转发全局广播，使用网段定向广播地址，例如 `192.168.31.255`。重新配对已有设备时，主机必须保留原设备 Token；丢失注册表时请先恢复出厂并重新配网。

## 按键输入回传检查

安装脚本会按操作系统检测输入回传能力：macOS 使用系统自带的 `osascript`，Linux 检测第一个可用的 `ydotool`、`xdotool`、`wtype`。如果不可用，脚本会继续完成主机文件、固件和 Hook 安装，但会明确提示：提醒显示正常，板子按键向电脑当前焦点窗口输入文字的功能不可用。

### macOS

macOS 不需要安装输入工具。首次使用时，请打开“系统设置 → 隐私与安全性 → 辅助功能”，允许实际启动 daemon 的 Terminal、iTerm 或其他终端/应用控制电脑。daemon 使用系统自带的 `osascript` 调用 `System Events` 输入文字；如果没有辅助功能权限，daemon 会提示授权位置，通知显示仍然正常。

可以先在当前焦点窗口测试：

```bash
osascript -e 'tell application "System Events" to keystroke "esphook-test"'
```

按桌面环境安装对应工具后，重启 daemon：

```bash
# X11
sudo apt install xdotool

# Wayland
sudo apt install wtype
```

也可以使用 `ydotool`；它还需要 `ydotoold` 常驻运行。安装脚本不会自动使用 `sudo` 安装系统包。daemon 运行时如果仍没有工具，只会提示一次，不会因为每个按键事件反复刷屏。

## 选项

```bash
# 只下载主机文件和固件
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh \
  | bash -s -- --no-hooks

# 只安装某些工具的 Hook
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh \
  | bash -s -- --tools claude,codex

# 指定 Release，便于回滚或复现
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh \
  | bash -s -- --release v0.0.0-123

# 已经明确同意修改 Hook 的自动化环境
curl -fsSL https://raw.githubusercontent.com/Ken-u/esphook/agent/install-agent-hooks/install.sh \
  | bash -s -- --yes
```

也可以下载 `install.sh` 后执行 `bash install.sh --help` 查看全部参数。`--yes` 是显式授权开关，日常交互安装不需要使用。

## Release 生成规则

CI 在 Pull Request 和非默认分支上只进行测试和固件构建。默认分支每次 push 在主机测试和 ESP32-C3 构建均成功后，自动创建一个编号形式的正式 Release：`v0.0.0-<GitHub Actions run number>`。因此 GitHub 的 `latest` Release 始终能被安装脚本发现。

## 固件使用

将 `esphook-firmware.bin` 上传到设备网页的 `Firmware OTA`，或执行：

```bash
curl --fail --data-binary @esphook-firmware.bin \
  -H 'Content-Type: application/octet-stream' \
  http://<设备 IP>/ota
```

如果需要更新中文字库、分区表，或设备只能通过 USB 访问，解压 `esphook-full-flash.zip`，按照其中的 `FLASH_LAYOUT.txt` 使用 ESP32-C3 兼容的 `esptool` 完整刷写。完整 OTA 限制和认证方式见 [ota.md](ota.md)，硬件接线见 [hardware.md](hardware.md)。
