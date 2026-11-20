# 用 AI Agent 上手、开发和配置

这个仓库按“让 AI Agent 直接干活”来整理：项目规则、操作步骤和安全边界都写成了 Agent 能读的文件，你不用记命令，告诉它想做什么，它按项目里的 skill 去刷机、配置、写应用、跑测试。

## 1. 开始

| 起点 | 做法 | 适合 |
| --- | --- | --- |
| 发布包（最快） | `gh release download -R Ken-u/esphook -p esphook-kit.zip -p SHA256SUMS`，核对 SHA256，解压后进 `esphook-kit/` | 只想刷机、配置，或写点小应用。里面有固件、源码、skills、测试 |
| 克隆仓库 | `git clone https://github.com/Ken-u/esphook.git`，固件到 [Release](https://github.com/Ken-u/esphook/releases/latest) 下载，或自己构建 | 要改框架，或提交改动 |
| fork + CI | fork 后在 Actions 页启用工作流，推送分支，CI 会构建固件并打包（`gh run download <id> -n esphook-release-packages`） | 改了固件源码，但本机没有 ESP-IDF |

然后：

1. 用 USB-C **数据线**接上板子（不接板子也能写应用和跑主机测试）。
2. 在这个目录里启动你的 Agent。项目入口是 [AGENTS.md](../AGENTS.md)：
   - Claude Code：读 `CLAUDE.md`，它引用 `AGENTS.md`；
   - Cursor、Codex 等：直接读 `AGENTS.md`，项目 skill 在 `.agents/skills/`；
   - 其他工具：让它先读 `AGENTS.md`，再按任务打开对应的 `SKILL.md`。
3. 直接说想做什么。

需要的环境：Python 3.10+（或 [uv](https://docs.astral.sh/uv/getting-started/installation/)，`./run.sh` 会自己选）。写应用、跑主机检查还要 C 编译器、libcjson 和 LVGL 源码，见 [README 的“测试”](../README.md#74-测试)。**不需要 ESP-IDF**：固件由 CI 构建。

## 2. 五个 skill

| skill | 什么时候用 | 它会做什么 |
| --- | --- | --- |
| `esphook-device` | 板子相关的一切 | 拿固件、校验、刷机；读状态、开应用、发文本命令、模拟按键；校时、切硬件配置、调屏幕、舵机排错；实测后恢复待机 |
| `esphook-hook` | 想让 Agent 完成/出错/等确认时屏幕提醒 | 起 daemon、配网配对、安装/卸载 Claude Code / Codex / Kimi Code / Cursor 的 Hook、发测试通知 |
| `esphook-app` | 写、改设备上的应用和游戏 | 用生成器起步，按框架契约写 C，用代码图标、输入、雷达、舵机、蜂鸣器等公共 API |
| `esphook-pc` | 电脑和板子联动 | USB SDK、应用消息、行协议、键盘转发、数据准备放电脑 |
| `esphook-check` | 提交前、交付前 | 一条命令跑全部主机检查，按改动挑重点，看 CI 与固件体积，CI 绿后刷机验证 |

支持项目 skill 的 Agent 可以直接点名（`$esphook-device`），其他 Agent 读对应 `SKILL.md` 就行。

## 3. 可以怎么说

**刷机和配置**

- 帮我把最新固件刷到板子上，再校准时间。
- 板子现在是什么状态？当前打开的是哪个应用？
- 把待机应用恢复成新婚快乐。
- 屏幕颜色整体反了 / 画面一行比一行歪，帮我调。
- 我接了舵机但没反应。（Agent 会让你切到 robot 配置、在设备上校准，它**不会**自己让舵机转动。）
- 只有 K1 能用，把 K2/K3 改成……（按键映射）。

**写应用**

- 做一个贪吃蛇：K1 转向，要有最高分和暂停，用代码图标，不要位图。
- 给“电子木鱼”加一个“连击”提示。
- 把赛车的转向改成用雷达控制。
- 加一个电脑端的数据脚本，把天气显示成待机页。

**Hook 与联动**

- 把我的 Claude Code 完成提醒接到这块屏上，先告诉我会改哪些配置文件。
- 把电脑上的某个窗口投到板子的屏幕上。

**验证和交付**

- 跑一遍全部检查，告诉我哪些是实机验证过的、哪些没有。
- 推送并等 CI，绿了就刷到板子上，最后恢复待机应用。

## 4. Agent 能做什么，不能做什么

| 能自己完成 | 需要你确认或在场 |
| --- | --- |
| 读写代码、跑 `./scripts/check-all.sh`、无屏 LVGL 渲染、协议与固件体积检查 | 声音好不好听（蜂鸣器只能确认定时器频率没被改动） |
| 通过 CI 构建固件、校验 SHA256、USB 刷机、读状态、开应用、发按键 | 舵机实际怎么转、方向和限位（只有你在场并断开舵盘后，才在设备上校准） |
| 配置硬件配置、时间、键位、屏幕模组参数 | 画面实际观感、帧率、按键手感、供电 |
| 起 daemon、配对、安装 Hook（先问你再改配置） | Wi-Fi 名称和密码、配对密钥（只从你这里取，不外传） |
| 提交、推送、看 CI | 擦除全部闪存、恢复出厂（`--erase-all`、`reset`），默认拒绝，要你明说 |

没连板子时，Agent 只会做主机侧工作，并明确写“没有连接开发板”，不会把没实测的结果说成已验证。

## 5. 开发循环

```bash
python3 scripts/new-app.py snake --title '贪吃蛇' --pc    # 生成 manifest、app.c、可选的 PC 程序
# 让 Agent 按 esphook-app 写代码
./scripts/check-all.sh snake                              # 只跑相关检查，快
./scripts/check-all.sh                                    # 提交前全跑，最后一行 ALL GREEN
git push                                                  # CI：主机测试 + 固件构建 + 体积预算 + 打包
gh run watch <id> --exit-status
./run.sh flash --dry-run && ./run.sh flash                # 把 CI 构建的固件刷进板子
./run.sh device open snake                                # 实机看一眼，再恢复待机
```

规矩（完整版见 [AGENTS.md](../AGENTS.md) 和 [应用框架](app-framework.md)）：应用放 `apps/<id>/firmware`，只用 `esphook/app*.h` 的公共 API；LVGL 和会话状态归 UI 任务；舵机默认断电；协议保持兼容；PC 的大数据和计算不编进固件。

## 6. 固件怎么来

固件只有一种：雷达和舵机驱动始终编入，运行时按硬件配置（`auto` / `screen` / `robot`）决定启用什么。构建以 GitHub Actions 为准：

- 默认分支成功后自动发布 Release：`v<version.txt>-<run 序号>`，附 `esphook-kit.zip`、`esphook-full-flash.zip`、`esphook-firmware.bin`、`SHA256SUMS`、`release.json`。
- 其他分支和 fork：从那次运行的 `esphook-release-packages` artifact 里取同样的包。Actions artifact 不是 Release，下载后同样先核对 `SHA256SUMS`。
- 想在本机构建：装 ESP-IDF v5.5.4，`idf.py set-target esp32c3 && idf.py build`，细节见 [README](../README.md#73-构建)。

`esphook-firmware.bin` 只含应用分区，只用于已联网板子的 [OTA](ota.md)，不要用它做首次 USB 刷机。

## 7. 安全默认值

- 舵机默认断电；只有你在设备上长按确认才会通电。Agent 不发 `robot arm/follow/center/angle:`，`run.py device cmd` 也会拒绝这类命令（退出码 4）。
- 刷机默认保留设置；`--erase-all`、`reset`（恢复出厂）要你明说。
- Wi-Fi 密码、设备配对密钥不进仓库、日志、提交和回答；配对密钥只经环境变量 `ESPHOOK_TOKEN` 传。
- 改全局 Agent 配置（装 Hook）之前先问你，原配置备份为 `.esphook.bak`，可 `--uninstall`。
- 实测之后把板子恢复成默认待机应用，并重新校时（板子没有电池）。

## 8. 排错

| 现象 | 看哪里 |
| --- | --- |
| Agent 说连不上板子 | `ls /dev/cu.usbmodem*`；串口被 `monitor`/`bridge` 占用时先停掉；换一根能传数据的线。见 [esphook-device](../.agents/skills/esphook-device/SKILL.md) 的“常见问题” |
| 检查缺依赖被 SKIP | 装 libcjson、克隆 LVGL v9.5.0；`CHECK_STRICT=1 ./scripts/check-all.sh` 会把 SKIP 当失败 |
| CI 体积预算失败 | 应用分区剩余不足 8%：看 `scripts/size-report.py` 的每应用体积，删减或精简 |
| 舵机不动 | [robot-servocal.md](robot-servocal.md) 的“舵机没反应” |
| Hook 通知到不了屏幕 | [hook-setup.md](hook-setup.md)，先看 `device cmd status` 里的 `link=` |
