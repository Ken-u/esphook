# TODO

已知的待办和限制。做完的从这里删掉；功能和用法的说明放 `docs/`，不要放在这里。

## 只能人来确认（Agent 和 CI 都看不到）

- 蜂鸣器音调和时长的听感：用菜单里的“蜂鸣测试”（扫频找谐振点、单音对比）在设备上试，再改 `framework/core/app_sound.c` 的音型表。
- 舵机的实际动作、方向和限位：只在用户确认并断开舵盘后，用“舵机校准”在设备上看，见 `docs/robot-servocal.md`。
- 标题/正文滚动（LVGL `SCROLL_CIRCULAR`）的速度和停顿、屏幕模组的偏色与偏移（`apps/screentest`）：看实际屏幕。
- 电源：用 USB 口带舵机时的掉压和复位，要用示波器或换供电来确认。

## Agent Hook / daemon

- `esphook setup daemon` 只提示“从用户服务或桌面自启动里运行 `esphook daemon`”；做 systemd user unit、launchd plist 或桌面自启动。
- 按键回传的词注入依赖主机工具（macOS `osascript`、Linux `ydotool`/`xdotool`/`wtype`）。Wayland 下要确认 `ydotoold` 常驻。
- `http_server.c` 的 `/notify` 对 `display_q` 使用非阻塞发送并忽略返回值：批量通知时队列满会返回成功但丢事件。需要时改成满了回 503。

## 构建与测试

- Unity 固件单测没有接入 release 构建：测试源在 `main/CMakeLists.txt` 里被注释掉，需要一个独立的 test 配置。目前的覆盖靠 `scripts/check-all.sh` 里的主机测试和无屏 LVGL 渲染。
- CI 没有实机：Wi-Fi、OTA 和 Hook 回键只能在板子所在网段用 `python3 tools/net_check.py` 手动验证。
