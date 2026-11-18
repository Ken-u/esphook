# 机器人显示协议 v1

同一套 JSON 可通过 USB CDC 和 HTTP 更新显示。显示协议只影响屏幕，不改变舵机 arm、跟随或校准状态；想静止观察雷达时先执行 `robot disarm`。当前显示设置只保存在 RAM，重启回自动表情，不需要重新刷固件来更换文字/图案。

## 两个传输入口

**USB CDC**：每条请求占一行，前缀 `ui `，后面紧跟 JSON。行尾 LF 或 CRLF。JSON 不超过 1024 字节（不含前缀、行尾），UTF-8；JSON 内换行用 `\n`，不能发送多行缩进 JSON。

```text
ui {"v":1,"mode":"face","expression":"happy","text":"任务完成","ttl_ms":5000}
ui {"v":1,"mode":"text","text":"构建完成\n可以烧录了","text_color":"#FDB927","ttl_ms":10000}
ui {"v":1,"mode":"radar","range_mm":6000}
ui {"v":1,"mode":"auto"}
```

成功回 `{"ok":true,"v":1}`；失败回 `{"ok":false,"v":1,"error":"原因"}`。无效 UTF-8、错误字段、重复字段、未知版本、过长请求和非法图案都会被拒绝，当前显示保留。传输层发现原始 NUL 或行超长时丢弃整行，回复 `ERR command too long or invalid byte`；硬件日志可能穿插输出，客户端只处理相应 JSON 回包即可。

快速命令：`robot radar` 等同 v1 radar 默认 6m，`robot face` 等同 auto；`ui status` 查询当前显示模式和雷达量程。切换不需要联网。

**HTTP**（先沿用 USB 的 `ssid:` / `pass:` / `connect` 配好 Wi-Fi）：

```bash
curl -X POST http://<设备IP>/api/v1/display \
  -H 'Content-Type: application/json' \
  --data-binary @examples/robot-ui/custom-face.json

curl http://<设备IP>/api/v1/radar
```

HTTP 直接发送 JSON，无 `ui ` 前缀，可缩进，整个 body 仍不得超过 1024 字节。成功 HTTP 200，格式/字段错误 HTTP 400。写入沿用现有认证：设备配置了 secret 时加 `X-Esphook-Token: <secret>`；未配置时保持原项目的局域网写入方式。`GET /api/v1/radar` 为只读诊断接口，不缓存响应。

## 显示请求字段

每次成功请求**完整替换**当前显示配置，未写字段回默认值，不继承上一次的文字、颜色或图案。

| 字段 | 取值 |
| --- | --- |
| `v` | 必填，整数 `1` |
| `mode` | 必填：`auto`、`face`、`text`、`radar` |
| `expression` | `auto`、`neutral`、`happy`、`sleepy`、`sad`、`custom`；默认 `auto` |
| `text` | 最多 192 个 UTF-8 **字节**，可用 `\n`；face 模式为底部字幕，text 模式为主屏文字 |
| `eye_color` | `#RRGGBB`，默认 `#C178FF` |
| `mouth_color` | `#RRGGBB`，默认 `#FDB927` |
| `text_color` | `#RRGGBB`，默认 `#FFFFFF` |
| `blink` | JSON 布尔值，默认 `true`；逐帧自定义动画建议 `false` |
| `ttl_ms` | 整数 0–600000；0 或省略表示直到下一条请求/重启；到期回自动表情 |
| `range_mm` | 雷达图满量程半径，整数 1000–6000，默认 6000 |
| `left` / `right` | `custom` 表情必填：7 行，每行 5 个 `0` / `1` 的字符串数组 |
| `mouth` | `custom` 表情必填：4 行，每行 5 个 `0` / `1` 的字符串数组 |

`auto` 是复位操作，只接受 `v` 和 `mode` 两字段，恢复机器人状态驱动的表情、默认颜色和眨眼。需要状态表情加字幕时用 `mode:face, expression:auto`。普通表情不能带自定义位图；`custom` 必须同时提供左右眼和嘴，`1` 点亮，`0` 透明。自定义表情仍有原来的粉色脸颊；视线随当前目标方向偏移。

text 模式必须有非空文字。字幕较长时滚动；主屏文字支持换行和长文本滚动。字符实际显示范围取决于原项目中文字库，UTF-8 合法不等于字体包含所有 emoji/生僻字。颜色按 RGB565 面板输出。

可连续发送表情帧实现客制动画，建议不超过 10 帧/秒，显示任务约 12.5Hz 采样最新请求。协议不是每帧必显示的播放队列；高频更新采用最新状态。HTTP/USB 同时更新时，最后完成的有效请求生效。所有 LVGL 操作仍在显示任务执行。

完整图案示例见 [custom-face.json](../examples/robot-ui/custom-face.json)。通过 USB 发该文件时先转成单行：

```bash
python3 -c 'import json; print("ui " + json.dumps(json.load(open("examples/robot-ui/custom-face.json")), ensure_ascii=False, separators=(",", ":")))'
```

## 雷达扫描页

输入 `robot radar`：

- **站在机器前面的人的视角**：雷达就是这台机器，所以画在屏幕**顶部中央**（一个带镜头的小机头），扇形向下张开，**你在机器的正前方、面对它**。因此你的右手边在屏幕右侧，离机器越远的点越靠下。这和传感器自己的坐标（传感器右为 +X、前为 +Y）相差 180°：画面把 X 镜像、把 Y 向下画，扇形 ±60°，三个同心弧分别为量程的 1/3、2/3、全量程，默认对应 2m / 4m / 6m。
- 右侧文字里的 X 也是**站在机器前面的人的左右**：向你的右边为正、向左为负（和画面一致），Y 是离机器的距离。HTTP/USB 的原始遥测仍是传感器坐标，没有改变。
- 金色、青色、品红色标记分别对应雷达原始槽位 1、2、3，旁边有相同编号；右侧每个槽位第一行是 `编号 + X米数`，第二行 `Y米数`。
- 显示的是雷达原始上报，不是跟随滤波后的坐标，也不套用跟随的 0.2–4m 筛选。超出当前画面量程、位于背面或超过 ±60° 的目标不会画在扇形内，右侧和 HTTP 仍可查看其原始坐标。
- 顶部 `R3 6.0m` 表示原始有效槽位数 3、画面满量程 6m。槽位编号会变化，不能当作人员身份。
- 800ms 没有完整有效上报时显示 `NO DATA`，隐藏目标点和旧坐标；收到正常空报告则立即清空槽位，在线目标数为 0。
- 扫线是显示动画，不驱动舵机扫描，也不代表雷达能区分扫线经过的测量时间。切换显示模式保持舵机原状态。

`robot face` 返回自动表情。缩小量程便于桌前近距离调试：

```text
ui {"v":1,"mode":"radar","range_mm":2000}
```

HTTP 原始诊断结构：

```json
{
  "v": 1,
  "mode": "radar",
  "online": true,
  "age_ms": 25,
  "frames": 123,
  "rejected": 0,
  "range_mm": 6000,
  "targets": [
    {"slot": 1, "valid": true, "x_mm": -1200, "y_mm": 2400, "speed_cms": 0, "resolution_mm": 320},
    {"slot": 2, "valid": false, "x_mm": 0, "y_mm": 0, "speed_cms": 0, "resolution_mm": 0},
    {"slot": 3, "valid": false, "x_mm": 0, "y_mm": 0, "speed_cms": 0, "resolution_mm": 0}
  ]
}
```

`online:false` 时三个槽位 `valid` 都为 false，数值可能保留最后一帧，仅供诊断，不可当成当前目标；从未收到完整报告时 `age_ms` 为 4294967295。雷达遥测独立于舵机是否开启以及当前显示模式。

## 固件空间与测试

新增扇形图使用一个 108×64 RGB565 缓冲区（13824 字节），不存整屏动画资源。OTA 槽现为两个 1600KiB，固定中文字库分区 768KiB，CI 会检查应用是否放得下。USB 完整刷写仍按原指南执行。

```bash
# 主机测试需要 libcjson-dev；固件沿用 ESP-IDF 自带 cJSON。
./scripts/test-robot-ui.sh
./scripts/test-robot.sh
```

显示协议测试覆盖 JSON 完整消费、版本、字段重复/未知字段、错误类型、UTF-8、超长/嵌套、像素图案、失败原子性和 TTL；雷达测试覆盖投影方向、量程、离线隐藏和边界坐标的安全裁剪。协议和坐标绘图已在主机验证，实体屏观感和串口/HTTP 交互仍需实机确认。

## 与 GPIO6 投篮模式共用屏幕

待机短按 GPIO6 开启动器，短按切换、按住 500ms 进入投篮 / 做 T / 雷达；任何页面三击回菜单。显示协议仍更新后台配置，菜单 10s 无操作恢复默认自动表情。见 [全局启动器](robot-launcher.md)、[投篮](robot-basketball.md)、[做 T](robot-stock-t.md)。
