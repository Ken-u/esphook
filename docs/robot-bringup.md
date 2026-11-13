# 桌面跟随机器人首版调试

本分支默认开启 `CONFIG_DESKTOP_FOLLOW_ROBOT`。硬件为 ESP32-C3 SuperMini（4MB）、原 ST7735 160×80 SPI 屏、LD2450 和 NHY009，无实体按键。电脑 USB 经同一个 Type-C 入口给整机供电。

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

所有模块共地，舵机不从 3V3 稳压输出取电。须核对 SuperMini 5V 排针和 Type-C VBUS 的实际连接。电脑 USB 口能否承受整机峰值电流尚待实测；本方案不引入额外电源。

固件不初始化原 GPIO6/10/11 按键，也不启动蜂鸣器。LEDC timer 1 / channel 1 用于舵机。应用日志改走 USB，避免 UART0 日志混入 GPIO21；ROM 启动日志仍可能在 GPIO21 出现，因此该脚只接雷达 RX，不能改接舵机信号。

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

## 首次校准（USB CDC，每行一条命令）

上电不输出舵机 PWM，重启后也始终保持 disarmed。先让舵机空载回中，再安装转接件/头部；主控不能回读 NHY009 实际轴角，第一次通电回中无法保证物理转速。

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
- 坐标低通滤波、1.5° 目标角死区及输出限速抑制抖动。单次调度延迟的补偿最多 100ms，避免卡顿后突然追赶。
- 正常空上报进入 lost，保持原目标角最多 1.5s，再限速回中；800ms 没有有效上报进入 radar_fault，并在保持期结束后回中。
- 收到新数据后重新建立过期的目标关联；不会把上一次断流前的目标直接当成当前人。
- 黑底紫色像素眼睛、金色嘴、粉色脸颊，带眨眼和视线偏移。跟随时笑眼，雷达故障时黄色叉眼。底部紫/金小标识对应关闭/开启输出。表情显示不依赖 Wi-Fi，机器人模式保持屏幕亮起。
- Wi-Fi 配网和 HTTP OTA 沿用原入口。本版不把 Hook 提醒覆盖到表情上，也不回传实体按键。

## 验证与边界

```bash
./scripts/test-robot.sh
python3 host/test_hooks.py
python3 host/test_device_link.py
```

纯 C 测试覆盖分包、粘包、坏帧重同步、嵌入帧尾、坐标符号、零速目标、槽位变化、目标丢失/断流恢复、范围过滤、脉宽限位和调度限速，并启用 AddressSanitizer/UBSan。CI 同时编译机器人与关闭机器人后的原 Hook 模式。某些受追踪容器无法运行 LeakSanitizer，可用 `ASAN_OPTIONS=detect_leaks=0 ./scripts/test-robot.sh` 保留地址/未定义行为检查。

尚未连接实机；UART 实物接线、PWM 频率/信号电平、转向和机械限位、静坐识别、噪音和电脑 USB 供电稳定性须上机验证。LD2450 无校验和，结构完整的错误帧不能全部识别；范围过滤和多帧锁定用于降低影响。
