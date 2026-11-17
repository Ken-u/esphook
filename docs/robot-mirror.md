# 副屏

把电脑上某个窗口（或屏幕的一块区域）实时投到 160×64 的小屏上：做看板、日志窗口、监控图表都行。画面由 PC 抓取、缩放、取色后经 USB 发给设备；设备只负责接收、校验和显示，PC 上没有任何东西编译进固件。

## 使用

```bash
python run.py mirror --list                         # 可投的窗口：id、应用、标题、尺寸
python run.py mirror --window Terminal              # 应用名或标题里含这段文字的最前窗口（macOS）
python run.py mirror --window 215                   # 或直接用窗口 id
python run.py mirror --region 100,100,640,256       # 屏幕上一块矩形（任何装了 Pillow 的系统）
python run.py mirror --screen                       # 整个屏幕
python run.py mirror --window Chrome --crop 0,120,800,320   # 只取窗口里的一块放大
```

`run.py mirror` 会自动在设备上打开“副屏”，不用先去菜单里选。常用参数：

| 参数 | 含义 |
| --- | --- |
| `--fit contain\|cover\|stretch` | 画面与 160×64 屏幕（2.5:1）的关系：留黑边（默认）、裁满、拉伸 |
| `--crop X,Y,W,H` | 先截取窗口/屏幕的这一块再缩放；整个窗口缩到 160×64 看不清字时用它放大 |
| `--fps N` | 最高发送帧率，默认 12，上限 60 |
| `--dither` | 照片类画面用误差扩散；文字和扁平界面不要开 |
| `--sharp` | Lanczos 缩放，边缘更锐，可能有振铃；默认 box 缩放 |
| `--refresh S` | 每隔 S 秒整屏重发一遍（默认 5），纠正任何丢失 |
| `--dry-run` | 不连设备：检查整条流水线，打印流量；配 `--preview out.png` 保存屏幕上会显示的样子（4 倍） |
| `--image FILE` | 来源是图片文件（每帧重读，可边改边看效果） |
| `--duration S` / `--frames N` | 到时停止 |
| `--menu-on-exit` | 结束时让设备回菜单；默认留在“电脑已断开”画面 |

屏幕上方 64 行是画面，下面一条状态栏：等待时是“等待电脑画面 / 电脑运行 run.py mirror”交替，有画面时显示“画面 N 帧/秒”和“单击暂停 三击退出”交替，超过 3 秒没有新数据显示“画面中断 等待电脑”，PC 正常结束显示“电脑已断开”。**K1 单击冻结画面**（再单击继续，冻结时 PC 自动停发），**三击回菜单**（框架手势，副屏不屏蔽它）。

## 平台与权限

- **按窗口抓取只支持 macOS**：用 Quartz 直接取目标窗口的内容，被别的窗口挡住也能取，需要 `pyobjc-framework-Quartz`（`run.py` 自动安装，装不上时仍可用 `--region`）和终端的“屏幕录制”权限。系统不提供旧接口时退回系统自带的 `screencapture -l`，较慢。
- 其它系统（以及 macOS）用 `--region` / `--screen`，只需要 Pillow（`run.py` 自动安装）。
- 取色是每 8 行一条独立调色板，16 色：文字、图表和扁平界面几乎不失真，照片会色阶化。

## 协议

行协议，和 USB 上其它命令一样每行 ≤1024 字节。

```text
mirror hello
mirror s <帧号 0..255> <条号 0..7> <crc16 四位十六进制> <base64>
mirror bye
```

- 屏幕分成 8 条、每条 8 行。一条的内容是 672 字节：16 个小端 RGB565 调色板字，加 160×8 像素的 4 位索引（左像素在高半字节，逐行）；base64 不带填充，共 896 字符，整行 915 字节左右。crc 是对这 672 字节的 CRC-16/CCITT-FALSE（初值 0xFFFF，多项式 0x1021）。
- 设备对每条命令回一行 JSON：`{"v":1,"type":"mirror_ack","ok":true,"f":12,"i":3,"free":5,"p":0}`。`ok:false` 时带 `err`：`busy`（环满了，稍后重发）、`crc` / `b64` / `args`（这行在路上损坏或格式不对，立即重发）、`inactive`（副屏不在屏幕上：被退出了）。`free` 是还能收几条，`p:1` 表示画面被 K1 冻结，这条被收下但丢弃，PC 不能把它算作已显示。`hello` 回复多一个 `op:"hello"` 和屏幕尺寸。
- PC 一条一条发（停等），只发和设备当前显示不同的条，整帧不变就什么也不发；`free` 为 0 时先等 12ms。ack 丢了就重发同一条，重复无害。
- 设备事件（`app_event`，app 为 `mirror`）：`enter`、`leave`、`pause`、`resume`。PC 在 `leave` 时结束，`enter`/`resume` 时重发整屏。

## 线程模型

- `command()` 在 USB 任务里执行：只校验行、把 base64 解到环里的空槽、回 ack，不碰 LVGL 也不读 UI 状态。环有 8 个槽（正好一整屏），用自旋锁保护索引，槽内数据在“提交”前不可见。
- UI 任务每次 tick 最多取 8 条画到共享画布并刷新；冻结时直接丢弃。LVGL 状态和画布只在 UI 任务里碰。
- 状态在框架 arena 里（远小于 2048 字节），环和计数在应用的静态区，每次进入应用都清空，不会把上一次的残留画到新画面上。

## 实现

- `apps/mirror/firmware/mirror_codec.{h,c}`：纯 C，CRC、base64、解析、展开和环；主机测试 `./scripts/test-mirror.sh`（ASan/UBSan）含 CRC 标准校验值、与 PC 编码器逐字节一致的样例行（`tests/data/mirror_strip.txt`）、拒绝畸形行、环回绕、4000 轮随机损坏不崩溃。
- `apps/mirror/firmware/app.c`：命令、环的消费、状态栏和图标。
- `apps/mirror/pc/`：`mirror_codec.py`（缩放、取色、编码）、`mirror_capture.py`（来源）、`mirror_stream.py`（协议、重试、差分）、`main.py`（命令行），测试 `python3 apps/mirror/pc/test_mirror.py`（需要 Pillow）；其中一项把固件的 C 解析器编成共享库，逐条解析 PC 编出的行并比较画面。

## USB 调试命令

```text
robot mirror status
```

返回 `active`、`live`（收到过画面）、`paused`、`fps`、`frames`（不同帧号的个数）、`strips`、`args` / `b64` / `crc`（各类坏行计数）、`busy`、`dropped`（冻结时丢弃）和 `age`（距上一条画面的毫秒数）。

## 验证

```bash
./scripts/test-mirror.sh                  # C 编解码与环
python3 apps/mirror/pc/test_mirror.py     # PC 编码、取色、协议重试、命令行，并与固件解析器交叉验证
./scripts/test-app-ui.sh                  # 真实 LVGL：等待画面、整屏、单条更新、坏行、12 帧/秒、冻结、中断、断开、离开
```

这些验证了协议、编解码和屏幕渲染；吞吐在真机上另测过：

- 最坏情况（每帧八条全部变化的渐变加移动圆，`--fps 30`）实测 **26～27 帧/秒**，14 秒发出 2960 条、2.6 MB，重发 0、忙 0（USB Serial/JTAG，ESP32-C3 SuperMini）。真实窗口大部分画面不变，靠差分只发变化的条，所以实际更轻松。
- 静止画面只在每 5 秒的整屏刷新时发 8 条，其余时间不占 USB。
- 副屏的环形缓冲（8 条）是静态内存，固件空闲堆比之前少约 5.5 KB（开机空闲 18 KB，`list` 目录回包瞬时最低约 10.8 KB）；流 2.6 MB 后堆最低值没有再下降。
