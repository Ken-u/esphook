# 桌面跟随机器人首版调试

通用构建默认编入 `CONFIG_DESKTOP_FOLLOW_ROBOT` 可选驱动，但运行配置默认 auto，保留旧三按键且不初始化 PWM。先运行 `python run.py hardware --set robot --reboot`（或设置应用配置页保存并重启），再执行下面的机器人调试。配置/检测状态详见 [发布指南](release-kit.md)。硬件为 ESP32-C3 SuperMini（4MB）、原 ST7735 160×80 SPI 屏、LD2450 和 NHY009，新增 GPIO6 游戏按键。电脑 USB 经同一个 Type-C 入口给整机供电。

## 默认接线

屏幕接线保持 [hardware.md](hardware.md) 不变。

| 模块引脚 | 主控引脚 / 电源 |
| --- | --- |
| NHY009 黑线 | GND |
| NHY009 红线 | USB 输入的 5V / VBUS |
| NHY009 黄线 PWM | GPIO10 |
| LD2450 5V / GND | 同一 USB 5V / GND |
| LD2450 TX | GPIO20（主控 UART1 RX） |
| LD2450 RX | GPIO21（主控 UART1 TX） |
| 游戏按键两端 | GPIO6 / GND（内部上拉，按下接地） |

所有模块共地，舵机不从 3V3 稳压输出取电。须核对 SuperMini 5V 排针和 Type-C VBUS 的实际连接。电脑 USB 口能否承受整机峰值电流尚待实测；本方案不引入额外电源。

机器人模式只扫描 GPIO6 单按键；GPIO10/11 的旧按键扫描不启动；GPIO7 的蜂鸣器照常工作（LEDC timer 0 / channel 0，不动舵机的 timer 1 / channel 1；两者都显式用 APB 时钟，见 `hw_pwm.h`），USB `sound` 命令会同时打印蜂鸣器和舵机定时器的实际频率，播放前后舵机应保持 50 Hz。LEDC timer 1 / channel 1 用于舵机。应用日志改走 USB，避免 UART0 日志混入 GPIO21；ROM 启动日志仍可能在 GPIO21 出现，因此该脚只接雷达 RX，不能改接舵机信号。

## 构建与刷写

使用现有 CI 对齐的 ESP-IDF v5.5.4：

```bash
source "$IDF_PATH/export.sh"
idf.py set-target esp32c3
idf.py build
idf.py -p <串口> flash monitor
```

完整烧录包括固定字库分区。已有工程缓存需要删除旧 sdkconfig 或通过 `idf.py menuconfig` 显式开启 Desktop follow robot；IDF defaults 不覆盖已有配置。menuconfig 可配置 UART 引脚、波特率、舵机引脚和 PWM 频率。

默认 UART1 为 256000、8N1。固件只接收标准 30 字节上报，不修改雷达的模式、波特率或固件。雷达若已被改过参数，先用厂商工具恢复标准坐标上报；透明传输固件未验证。模块最多上报三个目标，槽位不当成人员 ID。

## 首次校准（设备上 K1，或 USB CDC 命令）

**设备上**：装了舵机的机器人第一次开机会先进屏幕设置，再进“舵机校准”，只用 K1 调中位、摆幅、方向和速度，保存后才进待机，见 [robot-servocal.md](robot-servocal.md)。也可以随时从菜单最后的“舵机校准”重调。下面是等价的命令行做法。

## 首次校准（USB CDC，每行一条命令）

上电先关闭舵机 PWM。保存校准后，待机或表情页检测到人会自动跟随，进入支持震动的游戏会启用舵机反馈。首次校准先让舵机空载回中，再安装转接件或头部。主控不能回读 NHY009 实际轴角。

```text
robot status
robot limit:10
robot arm
robot angle:5
robot angle:-5
robot center
robot disarm
```

确认转向与机械空间后，按需调整：

```text
robot reverse:1
robot center_us:1500
robot limit:30
robot speed:35
robot save
robot arm
robot follow
```

`reverse:1` 仅在实测方向相反时需要；默认是 0。每条命令会回复 OK 或 ERR，应等回复再发下一条。

| 命令 | 行为 |
| --- | --- |
| `robot help` / `robot status` | 显示命令或雷达状态、坐标、帧数、输出角度及配置 |
| `robot arm` | 从关闭状态开启 PWM，目标中位，进入手动模式 |
| `robot follow` | 已 arm 时开始跟随 |
| `robot center` | 已 arm 时切手动并缓慢回中 |
| `robot angle:<整数角度>` | 已 arm 时在设定范围内手动转头 |
| `robot disarm` | 关闭 PWM；这不是断开舵机 5V，也不能保证舵机松力 |
| `robot center_us:<1400..1600>` | 关闭输出时修改中位脉宽 |
| `robot limit:<5..45>` | 关闭输出时修改左右角度上限，默认 ±30° |
| `robot reverse:<0或1>` | 关闭输出时修改方向 |
| `robot speed:<5..90>` | 关闭输出时修改最大逻辑转速，默认 35°/s |
| `robot save` | 保存校准到独立 NVS namespace；不保存 arm/follow 状态 |

中位加角度范围的组合必须全落在 1000–2000 μs；例如中位偏到 1550 μs 时不能使用完整 ±45°。不开放截图中 500–2500 μs 极限行程。50Hz 是工程初值，厂家截图尚未提供频率与 3.3V PWM 高电平阈值；需要实机或完整说明书确认。

`robot status` 显示的是计算/命令角度，不是实测轴角。校准不执行 `robot save` 则只在本次运行有效。原 `reset` 命令清除原 Hook 配置；机器人独立校准需要用上述设置命令修改后重新 save。

## 跟随与显示

- 雷达固定在底座正面。默认选择前方 0.2–4m、±60° 内的目标；连续三帧位置接近后锁定。
- 锁定后根据空间位置关联，允许目标换槽位；每帧关联距离门限 700mm。多人交叉仍可能换目标，不具备身份识别。
- 跟随使用低通滤波后的方位角，转头角度取估计方位的 28%，最多左右各 12°，转速最多 10°/s。锁定后，同向角差达到 2.5° 并持续 350ms 才更新目标。用户校准里更小的摆幅或速度优先生效。单次调度延迟的补偿最多 100ms。
- 退出自动运动的页面会关闭该页的舵机输出，新页面根据自身模式接手。用 USB 发 `robot disarm` 可关闭当前页面的输出，切换页面后按新页面的默认模式处理。
- 正常空上报进入 lost，保持原目标角最多 1.5s，再限速回中；800ms 没有有效上报进入 radar_fault，并在保持期结束后回中。
- 收到新数据后重新建立过期的目标关联；不会把上一次断流前的目标直接当成当前人。
- 黑底紫色像素眼睛、金色嘴、粉色脸颊，带眨眼和视线偏移。跟随时笑眼，雷达故障时黄色叉眼。底部紫/金小标识对应关闭/开启输出。表情显示不依赖 Wi-Fi，机器人模式保持屏幕亮起。
- Wi-Fi 配网和 HTTP OTA 沿用原入口。待机收到 Hook 通知时可进入 Hook 应用；游戏不会被打断。Hook 和按键应用均可回传，USB 虚拟按键也使用同一 SDK/手势框架。

## 自定义表情、文字与雷达页

USB 输入 `robot radar` 显示原始目标的扇形扫描页，`robot face` 返回自动表情。自定义显示使用版本化 JSON，USB 和 HTTP 共用；字段、位图样例和原始遥测见 [robot-display-protocol.md](robot-display-protocol.md)。切换显示不改变舵机状态。

## GPIO6 投篮游戏

待机短按 GPIO6 开启动器，短按切换投篮 / 做 T / 雷达，按住 500ms 画圆确认。任何页面三击回启动器，菜单无操作 10s 回眼睛表情动画。见 [全局按键](robot-launcher.md)、[两分钟投篮](robot-basketball.md) 和 [PC 做 T 游戏](robot-stock-t.md)。

## 验证与边界

```bash
./scripts/test-robot.sh
./scripts/test-basketball.sh
./scripts/test-robot-ui.sh  # 主机需 libcjson-dev
python3 apps/hook/pc/test_hooks.py
python3 apps/hook/pc/test_device_link.py
```

纯 C 测试覆盖分包、粘包、坏帧重同步、嵌入帧尾、坐标符号、零速目标、槽位变化、目标丢失/断流恢复、范围过滤、脉宽限位和调度限速，并启用 AddressSanitizer/UBSan。CI 同时编译机器人与关闭机器人后的原 Hook 模式。某些受追踪容器无法运行 LeakSanitizer，可用 `ASAN_OPTIONS=detect_leaks=0 ./scripts/test-robot.sh` 保留地址/未定义行为检查。

尚未连接实机；UART 实物接线、PWM 频率/信号电平、转向和机械限位、静坐识别、噪音和电脑 USB 供电稳定性须上机验证。LD2450 无校验和，结构完整的错误帧不能全部识别；范围过滤和多帧锁定用于降低影响。
