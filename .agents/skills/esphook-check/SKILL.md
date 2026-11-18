---
name: esphook-check
description: Validate esphook framework and app changes with manifest checks, lifecycle and input tests, real LVGL headless rendering, PC protocol tests and ESP32 firmware builds. Use when reviewing, debugging or preparing to ship esphook apps.
---

# 验证应用改动

先阅读 `docs/app-framework.md` 的接口与验证部分。根据改动范围选择检查，别把实机未测的结果写成已验证。

0. **先跑 `./scripts/check-all.sh`**（可加关键字只跑一部分，`--list` 看全部）：它就是 CI 的 `Host tests`，下面 1–5 步都在里面，最后一行 ALL GREEN 才算过；没有 `LVGL_SOURCE_DIR`/Pillow 时对应检查是 SKIP，不算完整运行。固件构建后用 `scripts/size-report.py` 看剩余空间和每个应用的体积（CI 低于 8% 失败）。
1. 执行 `python3 scripts/check-apps.py` 和 `git diff --check`，检查 manifest/id/源文件、descriptor、应用越层引用和 skills 元数据。
2. 执行 `scripts/test-app-games.sh`，确认公共头文件无 RTOS 也能编译、生命周期、单根节点所有权、延后导航、500ms 确认、三击、松手消费、10s 菜单超时和 NVS 保存失败时不切换。
3. 规则变化运行对应模型测试；共享服务变化回归 `test-robot.sh`、`test-basketball.sh`、`test-dino.sh`、`test-ski.sh`、`test-muyu.sh`、`test-haptic-steer.sh`、`test-aquarium.sh`、`test-fishing.sh`、`test-starship.sh`、`test-racing.sh`、`test-app-time.sh`、`test-app-util.sh`、`test-app-best.sh`、`test-app-screen.sh`、`test-fortune.sh`、`test-app-alarm.sh`、`test-alarm.sh`、`test-miner.sh`、`test-jump.sh`、`test-zombie.sh`、`test-stack.sh`、`test-mirror.sh`、`test-robot-ui.sh`。依赖为 C 编译器和 libcjson-dev，其他环境使用脚本的 CJSON_INCLUDE_DIR/CJSON_LIB 覆盖。
4. 设置 `LVGL_SOURCE_DIR` 为 v9.5.0 源码，运行 `scripts/test-app-ui.sh`。它编译全部已启用应用并用真实 LVGL/中文字体在 48KB UI 堆上反复切换；查看屏幕帧、控件裁剪和退出后的对象释放。它还会用真实加载代码校验 `main/fontdata.bin`（全部码点的字形指纹，以及 1500 份被改坏的镜像必须被拒绝或可安全查询）；有意换字库时同步更新指纹常量，见框架文档“换字或加字”。新增应用也由同一 manifest 发现。
5. 运行 `tests/test_app_sdk.py` 和 `tests/test_scaffold.py`，受影响的 `apps/<id>/pc` 测试（副屏的 `apps/mirror/pc/test_mirror.py` 需要 Pillow）、`--dry-run`；原 Hook 回归位于 `apps/hook/pc/test_*.py`。
6. 用 ESP-IDF 5.5.4 构建默认通用配置和 `CONFIG_DESKTOP_FOLLOW_ROBOT=n` 配置，或查看仓库 CI。查看应用分区剩余空间后再决定是否移除 OTA，不能仅看源码大小。
7. **CI 全绿后每次都要刷固件**（推送即自动发布 Release，默认分支）：
   1. `ls /dev/cu.usbmodem*` 找串口，`lsof <串口>` 确认没有 monitor/bridge 等进程占用。
   2. `gh release view --repo <owner>/<repo> --json tagName,targetCommitish`，确认最新 Release 的 `targetCommitish` 等于刚推送的提交；不是则等发布完成，或改用该次 CI run 的 artifact。
   3. 下载 `esphook-full-flash.zip` 和 `SHA256SUMS`，`shasum -a 256 -c` 校验后解压；在解压目录 `uv venv --python 3.11 --seed .esphook-venv`，`uv pip install --python .esphook-venv/bin/python -r tools/requirements-flash.txt`，先 `python flash.py --dry-run`，再 `python flash.py --port <串口>`。
   4. 刷后用 `AppLink` 查 `status` 和 `hardware`（含 `usb_tx_drops`）确认启动，按改动实测相关功能，最后回到默认待机应用（带 `APP_DEFAULT_STANDBY` 的应用，现在是 `wedding`；先 `release_keys()`、`menu()` 再 `open` 它，并用 `status` 确认 `active` 和 `standby`）。

回归已通过且没有新证据时停止重复测试。交付时说明分支/提交、实际通过的检查、固件体积，以及需要硬件确认的帧率、按键手感和电源/舵机表现。

发布相关变更还要运行 `tests/test_release_package.py`，检查解压后的 doctor、flash --dry-run、SDK 应用生成和 SHA256。USB 回复/串口发送改动运行 `scripts/test-usb-cdc.sh`（模拟 IDF TX 环的分片和丢帧上报）和 `scripts/check-apps.py`（`list` ACK 长度预算），必要时用 `hardware` 的 `usb_tx_drops` 在实机确认。硬件/按键改动运行 `scripts/test-hardware.sh`、`scripts/test-app-games.sh`、`tests/test_app_sdk.py`、`apps/key_report/pc/test_bridge.py`：auto/screen 不初始化 PWM，robot 仍默认 disarm，USB 取消不误发短按/确认。不要把 Actions artifact 当作已发布的 GitHub Release。
