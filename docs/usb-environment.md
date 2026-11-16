# USB 工具环境：推荐使用 uv

刷现成固件、运行 PC 工具，只需要 **Python 3.10+、esptool 5.1.0 和 pyserial 3.5**，不需要 ESP-IDF。推荐用 uv 准备独立虚拟环境，依赖只装在当前解压目录的 `.esphook-venv` 中。只有编译/开发固件才需要另装 ESP-IDF 5.5.4。

## 安装 uv（只需一次）

macOS 已有 Homebrew 时：

```bash
brew install uv
```

其他 macOS/Linux/Windows 安装方式见 [uv 官方安装指南](https://docs.astral.sh/uv/getting-started/installation/)。安装后重新打开终端，确认 `uv --version` 可用。

## 总包、SDK 或源码目录

进入含 `run.py` 和 `tools/requirements-flash.txt` 的目录。下面的烧录示例以总包为例；源码和 SDK 不自带镜像，只能先准备环境、运行 PC 工具，或自行构建后烧录。macOS/Linux：

```bash
uv venv --python 3.11 --seed .esphook-venv
uv pip install --python .esphook-venv/bin/python -r tools/requirements-flash.txt
.esphook-venv/bin/python run.py doctor
.esphook-venv/bin/python run.py flash --dry-run
.esphook-venv/bin/python run.py flash --port /dev/cu.usbmodemXXXX
```

Windows PowerShell：

```powershell
uv venv --python 3.11 --seed .esphook-venv
uv pip install --python .esphook-venv\Scripts\python.exe -r tools/requirements-flash.txt
.\.esphook-venv\Scripts\python.exe run.py doctor
.\.esphook-venv\Scripts\python.exe run.py flash --dry-run
.\.esphook-venv\Scripts\python.exe run.py flash --port COM4
```

`uv venv` 会在缺少 Python 3.11 时自动下载解释器。首次准备环境需要联网；`--seed` 同时准备 pip，兼容工具自带的依赖补齐入口。之后继续用该环境的 Python 执行 `run.py input`、`run.py stock`、`run.py hardware` 等命令即可，无需反复安装，也无需激活环境或修改系统 Python。

串口按实际设备替换；只有一个 Espressif 串口时可省略 `--port`。`doctor`、`--help`、离线 `--dry-run` 不连接 USB。

## 最小烧录包

解压后进入 `full-flash` 目录，仍使用上面的 `uv venv` 和 `uv pip install` 两行准备环境，再将后续命令中的 `run.py flash` 换成 `flash.py`：

```bash
.esphook-venv/bin/python flash.py --dry-run
.esphook-venv/bin/python flash.py --port /dev/cu.usbmodemXXXX
```

Windows 对应 `.\.esphook-venv\Scripts\python.exe flash.py --port COM4`。

## 可选组件和自动准备

- 键盘窗口（`run.py keys`、`stock --keyboard`）额外需要 Python 的 Tkinter/Tk 图形组件；uv 创建的虚拟环境不保证包含它。可用环境内 Python 执行 `-m tkinter` 检查；没有时仍可运行 USB 按键命令和不带 `--keyboard` 的 PC 游戏。
- 已有合适 Python 时，也可直接运行 `python3 run.py ...` / `py -3 run.py ...`，工具会通过 venv/pip 自动补齐 USB 依赖；上面的 uv 路径更明确，可提前一次准备好环境。
- 下载数据的 PC 应用仍需要网络；离线烧录不需要 ESP-IDF。把解压目录移动到别处或换电脑后，重新创建虚拟环境，不要分发 `.esphook-venv`。

uv 环境用法参考：[创建和使用虚拟环境](https://docs.astral.sh/uv/pip/environments/)。
