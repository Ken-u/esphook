---
name: esphook-device
description: Flash, configure and operate a connected esphook ESP32-C3 board from the command line - get a firmware, flash it over USB, read status, open apps, send USB commands and keys, set the clock, the hardware profile, the screen panel and servo calibration, and restore the default standby app. Use whenever a task touches the real board.
---

# 刷机、配置、操作板子

所有命令都在仓库（或解压后的 `esphook-kit`）根目录运行，入口是 `./run.sh <命令>`（自动选 Python ≥3.10，不够就用 uv；已有 3.10+ 时等于 `python3 run.py <命令>`）。**命令行参数一律用上面的子命令，不要自己写临时串口脚本**：它们已经处理了自动选口、回复被切断、日志混入和按键间隔。

## 0. 先看板子在不在

```bash
./run.sh doctor                   # Python、版本、有没有固件和 SDK；不连 USB
ls /dev/cu.usbmodem*              # Linux: ls /dev/ttyACM*；Windows: 设备管理器里的 COM 口
./run.sh device status            # 在线就会打印当前应用 active、待机应用 standby、硬件配置、堆、usb_tx_drops、时钟
```

没有串口、`status` 超时，就**如实告诉用户“没有连接开发板”**，只做主机侧检查，不要假装验证过。串口被别的程序（`monitor`、`bridge`、串口终端）占着时，`lsof <串口>` 看是谁，**让用户自己停，不要替他杀进程**。

## 1. 拿到固件

不需要 ESP-IDF，固件由 GitHub Actions 构建。三条路，从上往下选：

1. **上游发布版**（最省事）：`gh release download -R Ken-u/esphook -p esphook-kit.zip -p SHA256SUMS`，`shasum -a 256 -c --ignore-missing SHA256SUMS`（Linux `sha256sum -c ...`），解压后进 `esphook-kit/`，里面已有 `firmware/`，可直接刷。也可以只下 `esphook-full-flash.zip`，解压进目录，把下面的 `./run.sh flash` 换成 `python3 flash.py`。
2. **自己的分支或 fork**：推送后等 CI 绿（`gh run list --branch <分支>`、`gh run watch <id> --exit-status`），用 `gh run download <id> -n esphook-release-packages` 取出整套发布包（里面有 `esphook-kit.zip`、`esphook-full-flash.zip`、`SHA256SUMS`）；只有默认分支上的成功运行才会发 Release（`v<version.txt>-<序号>`）。fork 要先在仓库的 Actions 页启用工作流。刷之前确认固件对应你要的提交（`release.json` 里有 `source_commit`）。
3. **本机构建**（要装 ESP-IDF v5.5.4）：`idf.py set-target esp32c3 && idf.py build`，再用 `idf.py -p <串口> flash`。完整刷写包含固定字库分区，不要只写应用分区。

**只有 `esphook-firmware.bin` 不能用来 USB 刷机**，它只含应用分区，用于已联网板子的 OTA（见 `docs/ota.md`）。

## 2. 刷机

```bash
./run.sh flash --dry-run                   # 校验镜像和 manifest，只打印命令，不碰 USB
./run.sh flash --port /dev/cu.usbmodemXXXX # 只有一个 Espressif 串口时可省略 --port
./run.sh flash --bundle <解压目录> ...      # 固件不在默认位置时
```

- **默认保留板子上的设置**（Wi-Fi、配对密钥、校准、键位、最高分）。`--erase-all` 会全部擦掉，**必须先问用户**。
- 进不去下载模式：拔线，按住 BOOT 再插 USB（SuperMini 的 BOOT 在复位键旁）后重试；Windows 看设备管理器里是否出现新 COM 口。
- 刷完设备会重启进入待机应用。**不要刷到一半拔线**；失败就重新 `--dry-run` 再来。
- 修改固件源码后要“先 CI 全绿，再刷那一次提交的固件”，不要拿本地没经 CI 的产物当结果；维护者的完整流程见 `esphook-check` 第 4 节。

## 3. 读取状态、打开应用、发命令

```bash
./run.sh device status                    # JSON：app（当前 active、待机 standby）、hardware（配置、堆、usb_tx_drops）、clock
./run.sh device apps                      # 应用目录（id、标题、是否可作待机应用）
./run.sh device open <id>                 # 切到某个应用
./run.sh device menu                      # 回菜单
./run.sh device cmd "<一行文本命令>" [--expect <子串>] [--wait 秒]   # 逐行回复
./run.sh device status --dry-run          # 只打印将要做什么，不碰串口
```

- 退出码：`0` 成功，`3` 没有回复（加大 `--wait`，或板子在忙/重启），`4` 被安全检查拒绝（见第 6 节）。
- 按键：`./run.sh input tap|hold|triple|release [--key k1|k2|k3]`；一连串按键之间**留 ≥0.5 s**，否则被全局双击/三击手势吞掉。`./run.sh input keymap [--set '<json>']` 看/改实体键到逻辑键的映射。
- 持续看串口日志：`./run.sh monitor`。
- 常用文本命令（一行一条；`./run.sh device cmd` 发）：`status`（Wi-Fi/IP/daemon 连接）、`id`、`sound`（蜂鸣器测试并报告 PWM 频率）、`help`、`robot status`、`robot menu`；应用自己注册的命令前缀（例如屏幕测试的 `robot screentest ...`）见该应用的 `app_command` 注册和 `docs/app-pc-protocol.md`。
- 编程方式（需要连续操作、读事件）用 `framework/pc/esphook_sdk` 的 `AppLink`，见 `esphook-pc`。

## 4. 配置

| 想改什么 | 命令 | 说明 |
| --- | --- | --- |
| 硬件配置 | `./run.sh hardware`；`./run.sh hardware --set auto\|screen\|robot --reboot` | `auto` 默认，三键、探测雷达、舵机 PWM 不初始化；`screen` 纯屏；`robot` 把 GPIO10 交给舵机、只剩 K1，舵机仍需在设备上长按才通电 |
| 时间 | `./run.sh time` / `./run.sh time --show` | 板子没有电池，每次上电后闹钟和整点报时要重新校时 |
| 键位 | `./run.sh input keymap [--set ...]` | 保存在板子的 NVS |
| 屏幕模组（偏色、花屏、偏移） | `python3 apps/screentest/pc/main.py status\|plain\|plugin\|invert 0\|gap 0 24\|save\|reset\|mhz <n>\|raw ...` | 所见即所得，只有 `save` 才写入；两种常见模组见 `docs/hardware.md` |
| 电脑音频、副屏、股票 | `./run.sh mirror` / `./run.sh stock` / `./run.sh demo` | 见 README 第 5 节 |
| Wi-Fi、daemon、配对、Agent Hook | 见 `esphook-hook` | Wi-Fi 密码只从用户那里取，不猜、不存进仓库 |
| 蜂鸣器 | 在设备菜单打开“蜂鸣测试”，或 `./run.sh device cmd sound` | 能确认的是“定时器频率没被舵机改动”，**听感要人来判断** |
| 舵机 | 设备菜单“舵机校准”（先 `hardware --set robot --reboot`） | 见下面第 5 节 |

## 5. 舵机：只在用户确认后才会动

- 接了舵机却没反应，最常见原因是板子还在默认的 `auto` 配置：舵机 PWM 从未初始化。切 `robot` 配置并重启后，**还要在设备“舵机校准”第 1 页长按 500 ms 才会通电**。完整步骤、接线和五页校准见 `docs/robot-servocal.md`、`docs/robot-bringup.md`。
- **Agent 不要自己让舵机通电或转动**：不要发 `robot arm/follow/center/angle:`，也不要写会让舵机运动的脚本。上电前先让用户确认：已断开舵盘/机构、5 V 供电足够、线序对。`device cmd` 会拒绝这些命令（退出码 4），`--force` 只在用户明说“可以动”之后使用。
- 只读和安全的：`robot status`（`armed=1` 表示已通电）、`robot disarm`（立即断电）、`robot limit:/speed:/reverse:/center_us:`（只改校准，不通电）。

## 6. 安全红线

- 文本命令 `reset` 是**恢复出厂**（Wi-Fi、校准、屏幕设置、最高分全清）。`device cmd` 默认拒绝，只有用户明确要求才 `--force`。
- 密钥不外泄：设备配对密钥（64 位十六进制）和 Wi-Fi 密码不写进命令行历史、日志、提交或回答；脚本里用环境变量 `ESPHOOK_TOKEN`，来源是 `~/.config/esphook/devices.json` 里对应设备的 secret。
- 不替用户改网络、不扫描局域网找设备，要连网板子先问 IP。
- 不 `--erase-all`、不重新分区，除非用户明说；字库分区（`fontdata`）只用完整刷写更新。
- 实测之后把设备**恢复原状**：`./run.sh input release`、`./run.sh device menu`，打开带 `APP_DEFAULT_STANDBY` 的应用（`wedding`），`./run.sh time`，还原你改过的硬件配置、静音、闹钟。

## 7. 常见问题

| 现象 | 原因与处理 |
| --- | --- |
| `device status` 没有回复（退出码 3） | 串口被占用、选错口，或板子在重启/下载模式；拔插、`--port` 指定、加 `--wait 3` |
| 能刷但屏幕全白/偏色/左右错位 | 屏模组类型不同：用 `apps/screentest/pc/main.py plain` / `plugin` 切到对的那种再 `save` |
| 中文显示成空白 | 字库分区不是 v2（旧版分区布局）：做一次完整 USB 刷写，不要只 OTA |
| 刷机后设置丢了 | 用了 `--erase-all`，或换了不同的分区布局 |
| 按键没反应 | 当前是 `robot` 配置（只扫 K1）；或在菜单/确认等待里；`./run.sh hardware` 看配置 |
| 舵机不动 | 见第 5 节 |
| 连接状态里 `usb_tx_drops` 在增长 | 电脑没有及时读串口（`monitor` 已停但串口还开着）；关闭占用后重试 |
| Wi-Fi 板子在网页/OTA 上连不到 | 电脑和板子不在同一网段，先确认 IP（`device cmd status` 里 `ip=`）和路由 |
