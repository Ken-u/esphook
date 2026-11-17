# 做 T 高手：PC 行情 + USB 小屏

## 启动

刷入通用固件（无雷达/舵机也可运行），电脑通过原 USB-C 连接设备。在仓库执行：

```bash
./scripts/stock-t.sh
./scripts/stock-t.sh --port /dev/cu.usbmodemXXXX
```

脚本优先使用 `uv run --with pyserial`；没有 uv 时使用 Python 3，需要先 `python3 -m pip install pyserial`。省略端口时自动匹配唯一的 Espressif USB 串口。关闭占用同一串口的 monitor / 烧录程序。无需 Wi-Fi。

PC 先准备并上传数据，设备短按开菜单，选“做T”，按住 500ms 进入。准备数据不会抢占当前页面。离开做 T 到菜单时 PC 暂停回放、取消未完成确认，重新进入再继续。PC 退出会关闭会话并回菜单；仅离开页面不会关闭 PC 程序。

```bash
# 限定随机股票池
./scripts/stock-t.sh --symbols sh600000 sz000001
# 不连接设备，验证真实数据准备
./scripts/stock-t.sh --seed 24 --dry-run
# 明确使用合成演示数据，不冒充真实行情
./scripts/stock-t.sh --demo
# 导入自己的历史五分钟 CSV
./scripts/stock-t.sh --csv history.csv
```

CSV 列名为 `day,open,high,low,close`，时间示例 `2026-10-20 09:35:00`，价格单位元；包含至少 119 根前置五分钟 K 线及完整交易日。不完整交易日跳过。

## 玩法

- 每局随机一只股票和一个完整历史交易日；一天 48 根五分钟 K 线，默认每秒推进一根，午休直接跳过。可用 `--interval` 改变回放速度。
- 开局展示此前 60 根背景和当天第一根，视口最多显示 48 根。MA30 为黄色，MA60 为青色；上涨红色、下跌绿色。图表带深蓝渐变背景、虚线网格、最新价引导线和标签、起点旗标，页眉显示代码/时间/最新价（红涨绿跌），页脚是持仓徽标、收益率和 30/60 均线图例。
- 初始虚拟本金 100,000 元，空仓。按下先暂停 K 线，右侧进度条从零开始，500ms 满后按当时屏幕 K 线收盘价满仓买入，并恢复回放；继续按住不会重复买入。提前松手为短按，立即满仓卖出。空仓卖出、持仓再次买入不成交、不增加次数。
- 每次按下均暂停，未确认短按松手后保留本根 K 线剩余时间；成交后也保留剩余时间。成交使用设备上报的已显示游标，避免 USB 延迟导致用尚未看到的价格成交。
- 可在一天内反复满仓买卖，初版不计费用、不限制整数手，使用 Decimal 计算资金和可分割份额。最后一根显示完后按收盘价计价结算，显示收益率、盈亏金额和买卖成交总次数。
- 结算框长按 500ms 随机加载下一局，短按退出到菜单。任何页面三击回菜单。详见 [全局按键](robot-launcher.md)。

这是按需求设计的**虚拟 T+0 游戏规则**，不连接券商或真实账户。普通 A 股买入后不能在交收前卖出，游戏没有实现这一限制；参见 [上交所交易规则 3.1.4](https://www.sse.com.cn/lawandrules/sselawsrules2025/stocks/exchange/c/c_20260424_10816482.shtml)。

## 数据与内存

PC 用 Python 标准库访问新浪公开行情接口：缓存 A 股列表一天，五分钟行情六小时，从最多八只随机股票尝试准备数据，只选完整历史日；网络或源数据失败明确报错，不自动替换成演示数据。接口可用性及分钟历史范围取决于数据源。第一次下载股票列表可能较慢，限定 `--symbols` 可跳过此步骤。

每局向 ESP 上传 60 根背景 + 48 根日内 K 线。MA30/MA60 在 PC 上逐根只用当时及过去的收盘价计算，前置 119 根源数据保证背景第一根也有完整 MA60；未来数据变化不会改变已经显示的均线。ESP 的视图只复制当前及过去的窗口，不显示未来 K 线。

| 职责 | ESP32-C3 | PC |
| --- | --- | --- |
| 输入与导航 | IRQ 消抖、三击、500ms 确认、启动器 | 接收通用输入事件 |
| 图形 | LVGL 页面、代码图标、K 线/雷达绘制 | 数据准备 |
| 行情与指标 | RAM 收包和有限窗口 | 随机选股/交易日、缓存、MA30/MA60 |
| 做 T 逻辑 | 展示状态与收益摘要 | 时钟、暂停、满仓买卖、Decimal 资金、结算 |
| 存储 | 数据集最多 128 根；不写 Flash | CSV / 行情缓存 |

雷达和 K 线共用一个 15,360 字节 RGB565 绘图缓冲；数据集使用活动区与上传暂存区，合计约 6.5KB RAM，上传完整后原子提交。重复玩、换股票、改 PC 策略都不烧录固件。当前保留 OTA 分区；后续先通过 CI 的应用体积观察是否需要取消槽位。

后续游戏可复用 `GameLink`、`app_input`、应用导航及原子上传协议；数据密集的玩法优先放 PC，简单离线玩法像投篮继续放设备。当前图表协议服务于 K 线程序，并不是动态执行任意新游戏的脚本引擎；增加全新图形类型仍需增加相应固件渲染器。

## USB 协议 v1

每行 `game ` + UTF-8 JSON + 换行，JSON 最大 1024 字节；`v:1`、正整数 `seq` 和 `id` 必填。固件返回同 `seq` 的 `game_ack`；`selected` 表示做 T 是否在前台。主机最多一条未确认请求，忽略普通固件日志。

| op | 字段与语义 |
| --- | --- |
| `begin` | `count` 1..128，`first` 起始游标，`label` ≤48 UTF-8 字节，`code` ≤8 字节，`date` YYYY-MM-DD；使用新 id，仅分配暂存区 |
| `bars` | `offset` 和最多 12 行 `bars`；每行为 `[open,high,low,close,ma30,ma60]`，整数分；整包校验后写入 |
| `show` | `cursor,state,held_ms,holding,trades,pct_bps,baseline_bps,pnl_cents,clock`；state=`play/result/loading`，held_ms=0..500，收益单位基点、金额单位分；完整数据才可切为活动会话 |
| `heartbeat` | 续期当前 id；5s 无刷新显示 PC 断开，短按可回菜单 |
| `close` | 关闭当前 id 并回菜单 |

设备发 `game_button`，字段包含 `v,type,seq,id,action,pressed,ms,cursor`。action=`down/up/hold/menu/launch`；ms 是设备单调毫秒，cursor 是屏幕游标。`hold` 是固件的 500ms 确认；`menu/launch` 同步 PC 暂停状态。上行下行 seq 分别独立。此 USB 协议用于直接连接电脑，不通过 HTTP 暴露。

## 验证

```bash
python3 apps/stock/pc/test_stock_t.py
./scripts/test-app-games.sh
./scripts/stock-t.sh --demo --dry-run
```

覆盖均线因果性、48 根完整交易日、满仓盈亏、按住暂停/继续、成交不重复、已显示价格、JSONP 按纯 JSON 解析、USB 分包与混合日志、上传不抢焦点、缺包拒绝提交、过期会话、心跳和图表边界。CI 额外构建机器人和原 Hook 两种固件。实机串口延迟、屏幕可读性和实际帧率仍待刷机验证。
