# esphook 硬件与接线

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

当前 ST7735 初始化使用横屏、上下方向已校正的 `swap_xy + mirror_y` 配置。若你的屏幕排线或 ST7735 变体方向不同，优先调整 `main/st7735_params.h` 和 `main/display_view.c` 的面板参数，不要先改 UI 坐标。

无新提醒 10 分钟后，固件关闭 ST7735 输出和 `BLK` 背光；按任一按键或收到新事件会唤醒。屏幕和 ESP 共用 3.3 V/GND，USB 仍可用于日志、配网和重新烧录。
