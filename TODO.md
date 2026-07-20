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
- [x] **CC hooks 自动接入**:`esphook setup claude` / `scripts/install-hooks.sh` 幂等写入 `~/.claude/settings.json`
- [x] **Codex hook 体系调研**:使用 `~/.codex/hooks.json` 的 `Stop` / `PermissionRequest` command hooks
- [x] **session_id 来源**:Hook stdin 使用原生 `session_id`；Cursor 使用 `conversation_id`；直接 Codex CLI 使用 `CODEX_THREAD_ID`

## 暂缓问题

- [ ] **蜂鸣器队列无人消费**:`beeper_q` 目前只有生产者,需要补 beeper task 或明确改为直接调用
- [ ] **daemon 注入命令错误**:`esphook daemon` 的 socat 路径调用不存在的 `esphook-inject`
- [ ] **daemon setup**:`setup daemon` 仍只提示，需要另做 systemd user unit / desktop autostart
- [ ] **HTTP body 可能被截断**:`http_server.c` 的路由只调用一次 `httpd_req_recv`,需循环读满 content length
- [ ] **队列满时静默丢事件**:`display_q`/`beeper_q` 使用非阻塞发送且忽略返回值,批量通知时可能返回成功但实际丢失
- [ ] **Unity 单测未接入 release build**:测试源在 `main/CMakeLists.txt` 中被注释,需补独立 test 配置

## 调参

- [ ] 标题/正文滚动节奏(LVGL `SCROLL_CIRCULAR`):实机调速度和停顿
- [ ] 蜂鸣器音调/时长(`beeper.c`):NOTIFY/KEY/ERROR 三种,实机听感调
- [x] **屏幕布局重做**(`display_view.c`):LVGL 大字号状态/标题/正文布局已落地,剩余只做实机微调
- [x] **中文通知显示**:中文字库编译进字体 C 文件并由 `fontdata` 固定分区加载

## 已完成

- [x] 固件:USB Serial/JTAG CDC / config_store(NVS) / display_model(红点状态机) / display_view(ST7735) / net_wifi / beeper / http_server / input_task / display_task
- [x] 19 个 unity 单测(config_store 8 + display_model 11)开发期通过,release 构建禁用省 flash
- [x] host `bin/esphook` 单脚本(provision/status/notify/dismiss/setup/daemon/test)
- [x] 设计文档 + 实施计划(`docs/` 仓库)
- [x] 根 README / projects/README 索引
