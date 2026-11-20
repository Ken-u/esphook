# esphook

ESP32-C3 + 0.96 寸 ST7735 小屏的应用/小游戏框架：开机是待机应用，菜单里有 20 多个应用，可接雷达（LD2450）和舵机（NHY009）做机器人，也可作 Agent 提醒屏。所有应用共用一套生命周期，一个通用固件。**先读 [框架文档](docs/app-framework.md)，人类入门看 [README](README.md)，用 AI Agent 上手看 [Agent 上手指南](docs/ai-agent-guide.md)。**

## 按任务选 skill（`.agents/skills/<名>/SKILL.md`）

| 要做什么 | 用哪个 |
| --- | --- |
| 写/改设备应用、小游戏、图标、输入、雷达、舵机、蜂鸣器 | `esphook-app` |
| 写 PC 配套、USB SDK、应用消息、让板子和电脑联动 | `esphook-pc` |
| 刷固件、读状态、打开应用、配硬件/时间/屏幕/舵机、恢复待机 | `esphook-device` |
| 配 Wi-Fi、daemon、配对、安装 Claude/Codex/Kimi/Cursor 的 Hook | `esphook-hook` |
| 跑测试、验证改动、看 CI、交付前检查 | `esphook-check` |

Claude Code 等读 `CLAUDE.md` 的工具会引用本文件；skill 文件也可以直接当文档读。

## 目录

```text
apps/<id>/firmware   设备应用（manifest.json + app.c）      apps/<id>/pc   该应用的 PC 配套
framework/           应用框架：include/esphook（公开 API）、core、services、drivers、ports、pc/esphook_sdk
main/  cmake/        ESP-IDF 工程入口、USB/Wi-Fi/HTTP/OTA、字库（fontdata）
tests/  scripts/     主机测试；scripts/check-all.sh 是全部检查的总入口
tools/ run.py        刷机、USB 输入、设备操作、监视、打包等命令行（./run.sh <命令>）
docs/                文档，从 README 的“文档索引”进入
```

## 常用命令

```bash
python3 scripts/new-app.py <id> --title '<名称>' --pc     # 新应用起点
./scripts/check-all.sh                                    # 全部主机检查，最后一行 ALL GREEN
./scripts/check-all.sh <名字片段>                          # 只跑一部分；--list 看有哪些
./run.sh device status | apps | open <id> | menu | cmd "<一行命令>"   # 看/操作已连接的板子
./run.sh flash --dry-run                                  # 校验固件镜像；去掉 --dry-run 才会写板子
```

固件只在 GitHub Actions（ESP-IDF 5.5.4）里构建，本机不必装 ESP-IDF。

## 必须遵守

- 应用放 `apps/<id>/firmware`，配套放 `apps/<id>/pc`，通用代码放 `framework/`；不要给中央运行器增加针对某个应用的 if/switch。新应用必须有 manifest 和 descriptor，状态放框架 arena，控件创建在 `ctx->root` 下，退出时释放资源。
- 所有 LVGL 和应用会话状态归 UI 任务所有；USB `command` 回调只校验、答复、请求切换（`app_command.h`）；跨任务数据只用 `app_sync.h` 的快照/临界区。
- 应用只 `#include` C 标准库、cJSON、自己的头和 `esphook/app*.h`；数学/随机/固定步长/画布/状态条/最高分用公共 API（见框架文档“公共 API 速查”），不要私有再写一份。硬件只通过公开服务控制。
- 待机、菜单、500 ms 确认、全局三击（回菜单）和双击（回上一个应用）由框架处理，应用不重复实现。
- **舵机上电默认 disarm，保留校准、限位和限速；只有用户在设备上明确确认才通电。Agent 不要自己让舵机通电或转动**，也不要执行 `reset`（恢复出厂）或 `--erase-all`，除非用户明说。
- PC 大数据与计算不编译进固件。优先复用字体、代码图标、确认圆和绘图缓冲。
- 保持现有 USB、Hook HTTP、device link 和股票 game v1 协议兼容。
- 密钥不外泄：Wi-Fi 密码只从用户那里取，设备配对密钥只经环境变量 `ESPHOOK_TOKEN` 传递，不写进仓库、日志、提交或回答。
- 运行与改动匹配的检查，详见 `esphook-check`。**没连板子时，明确区分“模型/无屏渲染/固件构建”和“实机结果”，不要把没实测的写成已验证。**

## 维护者的交付流程

每次提交推送、**CI 全绿之后必须把该提交的 CI 固件刷到已连接的开发板**，做与改动相关的实机验证，再交付，并把板子恢复到默认待机应用（带 `APP_DEFAULT_STANDBY` 的应用，目前是 `wedding`）。步骤见 `esphook-check` 第 4 节，具体命令见 `esphook-device`。没有连接开发板或刷机失败时如实说明。
