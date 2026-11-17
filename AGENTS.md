# esphook application development

本仓库是 ESP32-C3 + ST7735 的应用/小游戏框架，机器人和旧 Hook 硬件使用同一套应用生命周期。先读 [框架文档](docs/app-framework.md)。

项目内可直接使用以下 skills：

- `.agents/skills/esphook-app/SKILL.md`：设备应用、游戏、代码图标、输入、雷达和舵机接口。
- `.agents/skills/esphook-pc/SKILL.md`：PC 数据处理、USB SDK、应用消息与事件。
- `.agents/skills/esphook-check/SKILL.md`：应用契约、生命周期、LVGL 渲染与固件验证。

应用实现放 `apps/<id>/firmware`，配套放 `apps/<id>/pc`，通用代码放 `framework/`。用 `python3 scripts/new-app.py <id> --title '<名称>' --pc` 起步；不要给中央运行器增加针对新应用的 if/switch。

所有 LVGL 和应用会话状态归 UI 任务所有；USB command 回调仅校验、答复、请求切换（`app_command.h`），跨任务数据只用 `app_sync.h` 的快照/临界区。应用只 include `esphook/app*.h`，数学/随机/固定步长/画布/状态条用公共 API（见框架文档“公共 API 速查”），不要私有再写一份。仅通过公开服务控制硬件。待机、菜单、500ms 确认和全局三击由框架处理。舵机上电默认 disarm，保留校准、限位和限速。

新增应用必须有 manifest 和 descriptor；状态放入框架 arena，控件在 ctx->root 下创建，退出释放资源。PC 大数据与计算不编译进固件。优先复用字体、代码图标、确认圆和绘图缓冲。

运行与改动匹配的检查，详情见框架文档。未连接硬件时，明确区分模型/无屏渲染/固件构建与实机结果。保持现有 USB、Hook HTTP、device link 和股票 game v1 协议兼容。

**每次 CI 全绿后都必须刷固件**：把该提交的 CI 固件刷到已连接的开发板，做与改动相关的实机验证，再交付，并把板子恢复到默认待机应用（带 `APP_DEFAULT_STANDBY` 的应用，目前是 `wedding`）。步骤见 `.agents/skills/esphook-check/SKILL.md` 第 7 步。没有连接开发板或刷机失败时如实说明，不要把未刷机的结果写成实机已验证。
