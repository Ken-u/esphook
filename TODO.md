# TODO — SuperMini AI-Hook

## 实机验证(插板后)

- [ ] 烧录 `idf.py -p /dev/ttyACM0 flash monitor`,看启动日志 + 屏显"no wifi cfg"
- [ ] `esphook provision <ssid> <pass>` 配网,屏切"connected + IP"
- [ ] `esphook status` / `esphook test` 验证 HTTP 通路 + 屏弹通知 + 蜂鸣
- [ ] 浏览器开设备 IP,配按键词 + 客户端/会话显示名,验证即时生效
- [ ] 两台机器同时 `esphook notify`,屏上红点分别计数不混
- [ ] 断线重连:拔路由器 → 屏离线 + LED 慢闪 → 恢复自动回连
- [ ] 串口直接发 `status`/`reset`/`help` 验证回显
- [ ] 批量发 33+ 不同 session,验证最旧被丢、不崩溃

### 实机已知风险点(代码没法验,插板才知)

- **ST7735 MADCTL(0x60) + RGB565 字节序**:颜色/方向错了调这两个(见 `display_view.c` st7735_init)。本板 160×80 的行地址偏移 `cy={0,26,0,154}` 按 MINI160x80 写的,可能要调
- **GPIO0 是 strapping**,屏 CS 接它,某些启动时序可能干扰下载——若下载异常复测
- 按键 GPIO6/10/11 消抖时序

## host 端

- [ ] **按键注入工具未装**:`ydotool`/`xdotool`/`wtype` 全无。`socat` 有(daemon 监听 OK),但收到 /keyevent 后词注入不进窗口。需先确定本机 **X11 还是 Wayland**:
  - X11 → `sudo apt install xdotool`
  - Wayland → `wtype` 或 `ydotool`(+ `ydotoold` 常驻)
- [ ] `esphook daemon` 实跑验证(socat 监听 + 注入闭环)
- [ ] daemon 开机自启(systemd user unit / desktop autostart)
- [ ] **CC hooks 自动接入**:`esphook setup claude` 当前只打印提示,没真正写 `~/.claude/settings.json` 的 hooks 段。需查 CC 当前 hooks 字段格式(Notification/Stop/UserPromptSubmit 的 matcher + command + stdin JSON),做成 setup 自动幂等写入
- [ ] **Codex hook 体系调研**:Codex 是否有等价 hook。无则 Codex 仅支持手动 `esphook notify`
- [ ] **session_id 来源**:CC/Codex hook 环境能否稳定拿到会话 ID;无则用"工具+PID+启动时间"兜底

## 调参

- [ ] 顶部状态栏滚动节奏(`dv_tick_scroll`):多 client 超屏时的速度/停顿,实机调
- [ ] 蜂鸣器音调/时长(`beeper.c`):NOTIFY/KEY/ERROR 三种,实机听感调
- [ ] 屏布局字号/行距(`display_view.c`):160×80 紧凑,实机看清晰度调

## 已完成

- [x] 固件:USB Serial/JTAG CDC / config_store(NVS) / display_model(红点状态机) / display_view(ST7735) / net_wifi / beeper / http_server / input_task / display_task
- [x] 19 个 unity 单测(config_store 8 + display_model 11)开发期通过,release 构建禁用省 flash
- [x] host `bin/esphook` 单脚本(provision/status/notify/dismiss/setup/daemon/test)
- [x] 设计文档 + 实施计划(`docs/` 仓库)
- [x] 根 README / projects/README 索引
