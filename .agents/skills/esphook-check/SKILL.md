---
name: esphook-check
description: Validate esphook framework and app changes with manifest checks, lifecycle and input tests, real LVGL headless rendering, PC protocol tests and ESP32 firmware builds. Use when reviewing, debugging or preparing to ship esphook apps.
---

# 验证应用改动

先阅读 `docs/app-framework.md` 的接口与验证部分。根据改动范围选择检查，别把实机未测的结果写成已验证。

1. 执行 `python3 scripts/check-apps.py` 和 `git diff --check`，检查 manifest/id/源文件、descriptor、应用越层引用和 skills 元数据。
2. 执行 `scripts/test-app-games.sh`，确认生命周期、单根节点所有权、延后导航、500ms 确认、三击、松手消费、10s 菜单超时和 NVS 保存失败时不切换。
3. 规则变化运行对应模型测试；共享服务变化回归 `test-robot.sh`、`test-basketball.sh`、`test-robot-ui.sh`。依赖为 C 编译器和 libcjson-dev，其他环境使用脚本的 CJSON_INCLUDE_DIR/CJSON_LIB 覆盖。
4. 设置 `LVGL_SOURCE_DIR` 为 v9.5.0 源码，运行 `scripts/test-app-ui.sh`。它编译全部已启用应用并用真实 LVGL/中文字体在 64KB UI 堆上反复切换；查看屏幕帧、控件裁剪和退出后的对象释放。新增应用也由同一 manifest 发现。
5. 运行 `tests/test_app_sdk.py` 和 `tests/test_scaffold.py`，受影响的 `apps/<id>/pc` 测试、`--dry-run`；原 Hook 回归位于 `apps/hook/pc/test_*.py`。
6. 用 ESP-IDF 5.5.4 构建默认通用配置和 `CONFIG_DESKTOP_FOLLOW_ROBOT=n` 配置，或查看仓库 CI。查看应用分区剩余空间后再决定是否移除 OTA，不能仅看源码大小。

回归已通过且没有新证据时停止重复测试。交付时说明分支/提交、实际通过的检查、固件体积，以及需要硬件确认的帧率、按键手感和电源/舵机表现。

发布相关变更还要运行 `tests/test_release_package.py`，检查解压后的 doctor、flash --dry-run、SDK 应用生成和 SHA256。USB 回复/串口发送改动运行 `scripts/test-usb-cdc.sh`（模拟 IDF TX 环的分片和丢帧上报）和 `scripts/check-apps.py`（`list` ACK 长度预算），必要时用 `hardware` 的 `usb_tx_drops` 在实机确认。硬件/按键改动运行 `scripts/test-hardware.sh`、`scripts/test-app-games.sh`、`tests/test_app_sdk.py`、`apps/key_report/pc/test_bridge.py`：auto/screen 不初始化 PWM，robot 仍默认 disarm，USB 取消不误发短按/确认。不要把 Actions artifact 当作已发布的 GitHub Release。
