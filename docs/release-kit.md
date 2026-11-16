# 通用固件发布包

同一份固件支持原来的屏幕/Hook 设备，以及增加 LD2450/NHY009 的机器人。默认 `auto` 配置保留 GPIO6/10/11 三个旧按键，只被动接收雷达 RX；**不会初始化舵机 PWM**。没有外设也能正常运行表情、Hook、投篮和 PC 小游戏，不会因为雷达无数据而显示故障表情。

## 下载哪个包

从当前版本的 GitHub Release 下载，先核对 release.json 的 version、source_commit 和 SHA256SUMS。

| 文件 | 内容 |
| --- | --- |
| `esphook-kit.zip` | 推荐总包：全量固件、烧录入口、PC 应用、SDK 源码、开发 skills、模板和测试 |
| `esphook-full-flash.zip` | 最小烧录包：bootloader、分区表、OTA data、应用、中文字库、manifest、Python 烧录入口 |
| `esphook-sdk.zip` | 最小开发源码包：应用/框架/驱动、CMake、依赖锁、字体、模板、项目 skills；不含 ESP-IDF 工具链 |
| `esphook-host.tar.gz` | 兼容旧安装器的 PC/开发源码归档 |
| `esphook-firmware.bin` | 仅应用，用于 OTA；首次刷机不要只刷这个文件 |

压缩包不包含整个 Python 解释器和 ESP-IDF。使用 Python **3.10+**；首次实际使用 USB 时会联网安装固定的 esptool 5.1.0 / pyserial 3.5 到包内隔离环境。macOS/Linux 可执行 run.sh，Windows 可执行 run.cmd；已有 uv 时也可由启动脚本准备 Python。`--help`、`doctor`、数据/固件 `--dry-run` 不需要 USB，直接用 Python 不会为这些检查安装依赖。

**推荐先用 uv 创建虚拟环境并安装依赖**，macOS/Linux、Windows 和最小烧录包的完整步骤见 [USB 工具环境](usb-environment.md)（总包根目录的对应文件为 `docs/usb-environment.md`）：

```bash
uv venv --python 3.11 --seed .esphook-venv
uv pip install --python .esphook-venv/bin/python -r tools/requirements-flash.txt
.esphook-venv/bin/python run.py flash --dry-run
```

按此方式准备后，下文 `python` / `python3` 命令请使用环境内的解释器：macOS/Linux 为 `.esphook-venv/bin/python`，Windows 为 `.\.esphook-venv\Scripts\python.exe`。刷现成固件和运行 PC 配套无需安装 ESP-IDF。

## 首次 USB 烧录

解压总包，进入 esphook-kit 目录：

```bash
python3 run.py doctor
python3 run.py flash --dry-run
python3 run.py flash --port /dev/cu.usbmodemXXXX
```

Windows 对应 `py -3 run.py flash --port COM4`。只有一个 Espressif 串口时可省略 --port。刷机前关闭占用同一端口的 monitor/游戏工具。脚本先验证每个镜像 SHA256、长度、地址范围和不重叠，再调用 esptool；地址来自本次构建的 flasher_args.json，不手写旧地址。默认保留 NVS 配置；只有明确传 `--erase-all` 才清空所有保存的信息。

若 460800 波特率不稳定，传 `--baud 115200`。设备没有自动进入下载模式时，按板子的 BOOT/RESET 操作进入下载模式再重试。最小烧录包则在 full-flash 目录运行 `python flash.py --dry-run` 和 `python flash.py --port ...`。

串口日志和命令：`python run.py monitor --port ...`。输入一行后回车会发送 USB 命令；`--read-only` 只读日志。

## 硬件状态和运行配置

设置应用有六页：版本、应用数量、Hook 信息、待机设置、硬件状态、硬件配置。

| 状态/配置 | 含义 |
| --- | --- |
| 屏幕“初始化/连接未知” | SPI 输出初始化成功，但没有读回通道，不能证明屏幕实际接上 |
| 雷达“已检测到” | 最近收到有效 LD2450 帧；无有效上报时为“尚未检测到”，驱动失败为“不可用” |
| 舵机“未知”或“初始化/连接未知” | NHY009 PWM 没有反馈信号，不能自动证明舵机实际接上；不能标成“已检测到” |
| auto（默认） | 三个旧按键；仅被动探测雷达 RX；不初始化 PWM，适合共享固件的安全默认值 |
| screen | 三个旧按键；不启动雷达 UART 和舵机；适合确定没有外设的旧设备 |
| robot | 只启用 GPIO6 按键，GPIO10 归舵机；初始化 UART/PWM 驱动但上电仍 disarm，须校准后显式 arm |

配置页长按进入编辑，短按选择，长按保存；再次长按重启生效。也可以通过 SDK/命令：

```bash
python run.py hardware
python run.py hardware --set screen --reboot
python run.py hardware --set robot --reboot
```

选 robot 前确认实际接线。GPIO10 不能同时接舵机信号和旧 K2 按键；切换配置需要重启后重新分配引脚。没有驱动支持或外设无数据时，服务返回不可用/未检测到，应用可继续运行。仅屏幕构建不允许选 robot。

## USB 虚拟按键和映射

```bash
python run.py input tap
python run.py input hold
python run.py input triple
python run.py input keymap
python run.py input keymap --set apps/key_report/pc/keymap.example.json
python run.py keys --config apps/key_report/pc/bindings.example.json
```

```bash
python run.py bridge                       # 终端实时转发：space/enter/tab 或 1/2/3 为 K1/K2/K3 短按，h+键长按，t 三击，m 菜单，q 退出
python run.py bridge --app key_report      # 先打开应用，再转发，并打印设备回传事件
uv run --with pyserial --with pynput python run.py bridge --backend pynput --config apps/key_report/pc/bridge.example.json
```

bridge 不依赖 Tkinter。终端后端看不到按键松开，所以每个键是一次短按；全局 pynput 后端有真实按下/松开，能做出真实长按，但会监听整台电脑的键盘且不能吞键，务必绑定专用键。

keys 打开一个仅在自身窗口有焦点时接收键盘的控制器，需要 Python Tkinter；没有 Tkinter 仍可用 input 命令和 SDK。默认 Space 对应 K1、Return 对应 K2、Tab 对应 K3，例子可将 Space/Return 都绑定到 K1。USB 模拟的是原始按下/松开，设备统一判定短按、500ms 长按和快速三击。实体键和 USB 键各自记录状态，某一边松开不会把另一边仍按住的键松开。

设备映射 physical/usb 分别将三个原始键映射到逻辑 K1/K2/K3（0/1/2），words 是三个逻辑键的 Hook 回传词；整体保存到 NVS。新的映射用于下一次按下，正在按住的键松开仍使用原映射。全局菜单手势属于逻辑 K1；改映射时保留可操作的 K1，必要时用 USB menu/keymap 恢复。

USB 按下有 **350ms 租期**，SDK hold/press 和 poll 会自动续期；手动 key_down 后若长时间不 poll/续期会取消而不产生短按或确认。窗口失焦、关闭和程序中断都取消按键。做 T 的真实 USB 会话只接受设备确认，取消不会误买卖。

## PC 小游戏与开发

```bash
python run.py demo
python run.py stock --demo --keyboard
python run.py stock --keyboard
python scripts/new-app.py counter --title '计数' --pc
```

`stock --demo` 是明确的合成数据；不加 --demo 使用随机 A 股历史数据。做 T 的键盘窗口复用同一条 USB 连接，不要另开第二个控制器占用串口。SDK 的 UsbLink/GameLink 也能调用 tap/hold/triple/keymap/hardware。

项目 skills 入口见 AGENTS.md。设备接口/生命周期见 docs/app-framework.md，PC 协议见 docs/app-pc-protocol.md。编译开发需要自行安装 **ESP-IDF 5.5.4**（或对应官方容器），然后在 SDK 根目录 `idf.py set-target esp32c3`、`idf.py build`。工具链和 LVGL 不打进最小源码包；依赖由 IDF 按锁文件获取。刷现成固件和运行 PC 工具均无需安装 ESP-IDF。
