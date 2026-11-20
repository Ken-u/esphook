---
name: esphook-check
description: Validate esphook framework and app changes with manifest checks, lifecycle and input tests, real LVGL headless rendering, PC protocol tests, firmware builds in CI and on-board verification. Use when reviewing, debugging or preparing to ship esphook changes.
---

# 验证改动

先读 `docs/app-framework.md` 的接口与验证部分。按改动范围选检查，**不要把实机没测的结果写成已验证**：交付时把“模型/无屏渲染/固件构建”和“实机”分开写。

## 1. 一条命令：主机检查

```bash
./scripts/check-all.sh            # 全部（就是 CI 的 Host tests）；最后一行 ALL GREEN 才算过
./scripts/check-all.sh racing     # 只跑名字里含 racing 的
./scripts/check-all.sh --list     # 看有哪些检查
```

- 需要 C 编译器和 libcjson（macOS `brew install cjson`，Debian `libcjson-dev`）；真实 LVGL 渲染测试要一份 v9.5.0 源码：`git clone --depth 1 --branch v9.5.0 https://github.com/lvgl/lvgl.git /tmp/esphook-lvgl`（`check-all.sh` 会自己找到）；副屏测试要 Pillow。缺依赖时对应检查是 SKIP，**不算完整运行**（`CHECK_STRICT=1` 让它失败）。
- 在 Agent 的 shell 工具里**前台**运行（耗时约 2 分钟，给足超时）；放到后台的进程可能在工具调用结束时被杀掉。
- `git diff --check`（空白错误）也在里面。

## 2. 按改动挑重点

| 改了什么 | 重点检查 |
| --- | --- |
| 应用（manifest、descriptor、依赖） | `python3 scripts/check-apps.py`：manifest/id/源文件、descriptor、越层 include、`list` ACK 长度预算、skills 元数据 |
| 生命周期、输入、导航 | `scripts/test-app-games.sh`（公共头无 RTOS 可编译、单根节点所有权、延后导航、500 ms 确认、三击、松手消费、菜单 10 s 超时、NVS 保存失败不切换）；硬件/按键相关再加 `test-hardware.sh`、`apps/key_report/pc/test_bridge.py`（auto/screen 不初始化 PWM，robot 默认 disarm，USB 取消不误发短按/确认） |
| 某个应用的规则 | 对应的 `scripts/test-<app>.sh`；共享服务变了就回归 `test-robot.sh`、`test-haptic-steer.sh`、`test-app-*.sh`、`test-stack.sh` 等 |
| 画面、字库、堆 | `scripts/test-app-ui.sh`（需要 `LVGL_SOURCE_DIR`）：编译全部已启用应用，用真实 LVGL 和中文字体在 48 KB UI 堆上反复切换，检查 label 缺字、对象释放、堆占用 < 80%，输出屏幕帧（PPM）；它还校验 `main/fontdata.bin`，有意换字库时同步更新指纹常量（框架文档“换字或加字”） |
| PC 配套、SDK、协议、`run.py device` | `tests/test_app_sdk.py`、`tests/test_device_tool.py`、`tests/test_scaffold.py`、受影响的 `apps/<id>/pc` 测试和 `--dry-run`；Hook 回归在 `apps/hook/pc/test_*.py` |
| USB 回复/串口发送 | `scripts/test-usb-cdc.sh`（模拟 IDF TX 环的分片和丢帧上报）、`scripts/check-apps.py`；必要时用 `hardware` 的 `usb_tx_drops` 在实机确认 |
| 发布包 | `tests/test_release_package.py`（解压后的 doctor、flash --dry-run、SDK 应用生成、SHA256） |
| 文档 | `python3 scripts/check-docs.py`（相对链接、锚点、文档索引） |

回归已通过且没有新证据时，不要重复测试。

## 3. 固件：以 CI 为准

- 固件由 GitHub Actions（ESP-IDF 5.5.4）构建，只有一个通用固件；本机不需要 ESP-IDF。CI 的 `size-report` 步骤在应用分区剩余空间低于 8% 时失败；看 `scripts/size-report.py` 的每应用体积，不要只看源码大小，再决定是否取舍功能。
- 推送后用 `gh run list --commit <sha>` / `gh run watch <id> --exit-status` 等结果。默认分支上的成功运行会自动发布 Release（`v<version.txt>-<run 序号>`）；其他分支的固件在该次运行的 artifact 里。**Actions artifact 不是已发布的 GitHub Release。**

## 4. CI 全绿后刷机验证（维护流程）

本仓库的约定：**每次 CI 全绿后，把该提交的固件刷到已连接的开发板，做与改动相关的实机验证，再交付，并把板子恢复到默认待机应用。** 没有连接开发板或刷机失败时如实说明，不要把未刷机的结果写成实机已验证。

1. 确认串口空闲：`ls /dev/cu.usbmodem*`（Linux `/dev/ttyACM*`），`lsof <串口>` 看有没有 monitor / bridge 占用。**不要替用户杀掉别的进程**，让他自己停。
2. 确认固件对应刚推送的提交：`gh release view --json tagName,targetCommitish`，`targetCommitish` 必须等于该提交；不是就等发布完成或改用该次运行的 artifact。
3. 下载 `esphook-full-flash.zip` 和 `SHA256SUMS`，`shasum -a 256 -c --ignore-missing SHA256SUMS` 校验，再按 `esphook-device` 的“刷机”步骤 `--dry-run`、刷入（`run.py flash` 保留设置，不要 `--erase-all`）。
4. 刷后用 `python3 run.py device status` 看启动（`hardware` 里的 `usb_tx_drops`、`heap_free/heap_min`），再按改动实测相关功能（`device open <id>`、`input tap/hold`、`device cmd "..."`；要连续操作就用 `AppLink`，按键之间留 ≥0.5 s，免得被全局双击/三击手势吞掉）。
5. 改动碰到 Wi-Fi/HTTP/OTA/Hook 回键时，还要在**板子所在的 Wi-Fi 网段**跑 `python3 tools/net_check.py --device <板子 IP> [--ota <新固件.bin>]`（`PYTHONPATH=framework/pc`；检查 `/health`、`/`、`/notify`、按键后板子向回调地址 POST `/keyevent`，以及可选的 OTA 重启）。板子 IP 看 `status` 的 `ip=`；已配对的板子要 `ESPHOOK_TOKEN`（取自 `~/.config/esphook/devices.json` 里该设备的 secret，**只用环境变量传，不写进命令行、日志或回答**）。电脑和板子不在同一网段就连不上：如实写“网络路径未实机验证”。
6. **恢复默认状态**：`release_keys()`、`menu()`，再打开带 `APP_DEFAULT_STANDBY` 的应用（现在是 `wedding`），用 `status` 确认 `active` 和 `standby`；板子没电池，顺手 `python3 run.py time` 校时；测试里改过的硬件配置、静音开关、闹钟等要还原。

## 5. 交付时说明

分支/提交、实际通过的检查、固件体积（`size-report`），以及仍需人确认的东西：声音听感、舵机实际动作、帧率、按键手感、电源。
