# esphook 硬件与接线

本页保留原屏幕/Hook 板接线。机器人扩展见 [机器人调试](robot-bringup.md)，通用固件下载与硬件配置见 [发布指南](release-kit.md)。默认 auto/screen 保留三按键且不初始化舵机 PWM；robot 配置重启后仅保留 GPIO6，将 GPIO10 交给舵机。通用构建不启用旧蜂鸣器，精简 legacy 构建保留。

## 硬件组成

- 控制器：ESP32-C3 SuperMini，4 MB Flash，使用板载 USB Serial/JTAG CDC。
- 显示：0.96 寸 ST7735 SPI，逻辑分辨率 160×80，3.3 V 逻辑。
- 输入：三个按键，固件使用内部上拉，按下时把 GPIO 拉到 GND。
- 声音：无源蜂鸣器，LEDC PWM 驱动。

固件中的 GPIO 是芯片 GPIO 编号，不是排针的物理序号。请以开发板丝印上的 `GPIO0`、`GPIO3` 等名称对应；不同 SuperMini 厂家的排针排列可能不同。

## 接线表

### ST7735 屏幕

| ST7735 引脚 | ESP32-C3 GPIO | 说明 |
| --- | ---: | --- |
| `VCC` | `3V3` | 只接 3.3 V |
| `GND` | `GND` | 共地 |
| `SCL` / `CLK` | `GPIO4` | SPI 时钟，20 MHz |
| `SDA` / `MOSI` | `GPIO5` | SPI 主出 |
| `CS` | `GPIO0` | 片选 |
| `DC` | `GPIO3` | 数据/命令 |
| `RES` / `RST` | `GPIO2` | 复位 |
| `BLK` / `LED` | `GPIO1` | 高电平亮，低电平灭 |

屏幕没有用到 `MISO`，可以悬空。屏幕模块若自带背光限流，仍然只接 3.3 V 逻辑；不要把 GPIO 直接接到 5 V。

#### 不同厂家的 0.96" 屏（颜色反了、画面一行比一行歪）

同样标 ST7735、同样的接线，0.96 寸 80×160 屏有两种常见版本，固件默认是写这套固件时用的那块（`plugin`）：

| 版本 | 颜色 | RAM 偏移 x / y |
| --- | --- | --- |
| `plugin`（默认） | 需要 INVON 反色命令 | 1 / 26 |
| `plain` | 不能反色 | 0 / 24 |

用错版本时有两个症状：**所有颜色整体反相**（暗红底变青绿、金字变蓝），以及因为窗口比 160 列的 RAM 多出一列，**画面一行比一行多歪一个像素**（竖笔画变斜线）。这不是接线问题（`SDA`/`SCL`/`DC`/`CS`/`RES` 接错会白屏或满屏乱码）。

屏幕调整保存在 NVS，换屏后调一次即可。

**新板子第一次开机会先进屏幕设置**（`screentest` 应用，固件在没有保存过屏幕调整时把它当作第一个画面），设好并保存之后才进入原来的流程（默认待机 `wedding`）。这样花屏时不用先在菜单里找到它。画面是白框、网格、四角彩色方块、色条和灰阶；白框要贴满四边、竖线要保持竖直、色条要是 红 绿 蓝 白 青 品 黄，下面一行 `n/4 INV|NORM` 和 `gap x,y` 是当前组合。

- **短按 K1**：换下一种组合，一共四种，按到画面正常为止：`1/4` 反色 + 偏移 1/26（默认，`plugin`）→ `2/4` 不反色 + 1/26 → `3/4` 不反色 + 0/24（`plain`）→ `4/4` 反色 + 0/24 → 回到 `1/4`。只有颜色反了的屏（常见）一般按一下就对。
- **长按 K1 500 ms（出现金色进度线）**：保存当前画面并继续，之后每次开机直接进待机，不再问。
- 首次设置里三连按是关掉的（防止误退出）；30 秒没有任何按键就不保存、直接进待机，下次开机再问，所以没人看着的板子也能正常启动。
- 之后想重调：菜单最后的“屏幕测试”（USB `robot screentest start` 也能打开），按键同上；这时长按保存并回到菜单，三连按等其他退出方式不保存，画面恢复成进入时的样子。想让板子下次开机重新走一遍首次设置：`python3 apps/screentest/pc/main.py reset`（恢复出厂也会清掉屏幕调整）。

不想用按键时用 PC（先把屏幕有问题的固件刷好）：`python3 apps/screentest/pc/main.py plain`（或 `plugin`）立即生效但不保存，看对了再 `python3 apps/screentest/pc/main.py save`（保存后首次设置就不会再出现）；还可以 `invert 0|1`、`gap <x> <y>`（0..63，微调偏移）、`status`（带 `configured=0|1`）、`reset`、`mhz <1..40>`（SPI 时钟，默认 20，保存后断电重上电生效，噪点严重可试 10）、`raw 2A 00 01 00 A0`（直接给屏控发一条命令做实验，断电恢复）。命令也可以用串口终端直接发 `robot screentest plain`。

从旧固件升级的、屏幕本来就正常的板子，升级后第一次开机也会问一次：长按 K1 确认当前组合即可，之后不再出现。

装了舵机的机器人（robot 配置）在屏幕设置之后还会接着进“舵机校准”，见 [robot-servocal.md](robot-servocal.md)；两步都做完才进待机。

### 按键与蜂鸣器

```text
K1 一端 ─ GPIO6       K1 另一端 ─ GND
K2 一端 ─ GPIO10      K2 另一端 ─ GND
K3 一端 ─ GPIO11      K3 另一端 ─ GND

无源蜂鸣器正端 ─ GPIO7（建议串 100~330 Ω）
无源蜂鸣器负端 ─ GND
```

按键映射默认为 K1=`ok`、K2=`continue`、K3=`done`，可以在设备网页配置。按键是低电平有效；不要再给按键外接上拉，固件已经启用内部上拉。

## 初次 USB 烧录

初次烧录或分区/字库变化时，在项目根目录执行：

```bash
source "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

`idf.py flash` 会写入 bootloader、分区表、应用、OTA 数据和 `main/fontdata.bin`。如果串口不是 `/dev/ttyACM0`，替换为实际设备；Linux 用户通常需要把当前用户加入 `dialout` 组。

初次刷写后，USB CDC 可以直接收发配置命令：

```text
ssid:<Wi-Fi 名称>
pass:<Wi-Fi 密码>
daemon_host:<主机 IP>
daemon_port:18765
daemon_secret:<64 位十六进制密钥>
connect
status
```

更推荐使用主机端的 `./bin/esphook provision ...`，它会生成密钥、登记 daemon 并通过 USB 一次写入上述配置。板子重启后会主动连接 daemon。设备已经配好 Wi-Fi 时，可使用 `./bin/esphook pair --server <主机IP[:18765]>` 通过 LAN 广播配对；如果主机能直接访问配置中的 `DEVICE_IP`，则加 `--esp` 改用 HTTP 直连。USB 仍用于首次写入 Wi-Fi、恢复出厂后的重新配网和完整烧录。

## 显示方向与省电

当前 ST7735 初始化使用横屏、上下方向已校正的 `swap_xy + mirror_y` 配置。若你的屏幕排线或 ST7735 变体方向不同，优先调整 `framework/ports/esp32/display_port.c` 的面板参数，不要先改应用 UI 坐标。

应用框架当前保持待机动画和屏幕活动；菜单 10s 无操作返回所选待机应用。屏幕和 ESP 共用 3.3 V/GND，USB 仍可用于日志、配网和重新烧录。
