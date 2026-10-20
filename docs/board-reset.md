# RK3588 RESET 按键控制

ESP32-C3 SuperMini 保留原来的提醒屏、三个按键和蜂鸣器，用空闲 **GPIO20** 控制一个外部电子开关，模拟按下 RK3588 的 RESET 按键。GPIO 编号以芯片/板上丝印为准，不是排针序号。GPIO20 同时是 UART0 RX；启用此功能后不要再接外部 UART TX 到这个脚。USB Serial/JTAG 使用 GPIO18/19，不受影响。

## 硬件接线

GPIO20 **不是干接点，不能直接跨接 RESET 按键**。固件输出为：空闲 0 V，触发约 200 ms 的 3.3 V 高电平，再回到 0 V。

### 按键一端已确认是 GND：小 N-MOSFET

例如使用适合 3.3 V 栅极驱动、足以吸收复位上拉电流的 2N7002：

| 接点 | 连接 |
| --- | --- |
| C3 GPIO20 | 经 330 Ω 电阻接 MOSFET G（栅极） |
| MOSFET G 与 S | 并接 4.7 kΩ 下拉电阻 |
| MOSFET S（源极） | RK3588 按键的 GND 端，同时连接 C3 GND |
| MOSFET D（漏极） | RK3588 按键的 RESET 信号端 |

保留原按键，MOSFET 并联在两个电气触点之间。不向目标 RESET 注入 3.3 V。MOSFET 封装脚号以购买器件手册为准。

### 按键电路未知，或希望隔离

选用**支持 3.3 V GPIO 高电平触发的 PhotoMOS 干接点模块**。控制输入接 GPIO20/C3 GND，输入加 4.7 kΩ 下拉；输出的两个干接点并联到 RESET 按键两端，输出侧不必与 C3 共地。不要直接用 GPIO 驱动机械继电器线圈，也不要使用低电平触发模块配合本固件。

四脚轻触按键一般有两组内部相连的脚：断电测量，找到未按下不通、按下才通的两个触点。不要接到同一组常通脚。

GPIO20 的 UART RX 默认有弱上拉（见 [ESP32-C3 数据手册的引脚配置表](https://documentation.espressif.com/esp32-c3_datasheet_en.html)），因此这里明确使用 **4.7 kΩ** 下拉，不使用先前泛用建议的 100 kΩ。4.7 kΩ 硬件下拉不能省略：它在 C3 上电、复位、刷机和未运行程序时保持开关断开。建议 C3 独立 USB 供电，避免 RK3588 复位时同时切掉控制器电源。先不接目标板，确认 GPIO20 空闲低电平、命令时短脉冲，再接 RESET。

## 软件使用

更新主机程序并重启 daemon，再给 C3 更新本版本应用固件（OTA 不改变现有 Wi-Fi 和配对配置）。原连接方式不变：C3 主动连接 daemon 的 TCP 18765。

```bash
# 查看在线设备
esphook status

# 仅有一台在线 C3 时
esphook reset-board

# 多台设备时显式指定，ID 从 status 的 reverse_online 获取
esphook reset-board --device-id <C3设备ID>
```

管理网页 `http://<daemon地址>:8787/` 的“重置开发板”按钮使用上方的 device_id，并在点击后确认一次。

也可调用主机 API（这里是 daemon 地址，不是 C3 地址）：

```bash
curl --fail-with-body -X POST http://127.0.0.1:8787/api/reset-board \
  -H 'Content-Type: application/json' \
  -d '{}'
```

指定设备的请求体为 `{"device_id":"<C3设备ID>"}`。沿用 daemon 的 `ESPHOOK_ADMIN_TOKEN` 配置；配置后 CLI 自动发送 `X-Esphook-Admin`，curl 需加同名请求头。现有管理网页没有 Token 登录界面，启用该鉴权时使用 CLI/API。未配置 Token 的管理服务仅应向可信网络开放。

## 完成与失败语义

- 每条命令只产生一个约 200 ms 的高电平脉冲，松开后冷却 3 秒；冷却期间拒绝新命令，不排队。
- 固件松开开关后才回 ACK；`ok: true, state: released` 说明脉冲执行结束，**不代表 RK3588 已成功启动**。
- 开关释放由 C3 本地任务完成，不依赖网络连接是否仍在。
- 仅走已经认证的 reverse device link，不向设备 HTTP 回退，不保存离线任务，不在重连时补发。
- 多台在线且没有指定设备、设备离线、固件拒绝时命令返回非零退出码。
- ACK 超时代表结果未知（也可能旧固件不支持）。请观察目标板状态后再决定是否重新执行；CLI 不会自动重试。
- 这是 RESET 按键模拟，不是切断电源。效果和实体 RESET 按键相同。

本版本软件测试验证的是协议处理和 GPIO 调用顺序；实际按键电压、电气接线与目标板复位效果需要接线后实测。
