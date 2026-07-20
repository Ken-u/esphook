#!/usr/bin/env bash
set -euo pipefail

HOME_DIR="${HOME:-}"
REPOSITORY="${ESPHOOK_REPO:-Ken-u/esphook}"
API_BASE="${ESPHOOK_API_URL:-https://api.github.com}"
RELEASE_REF="latest"
INSTALL_DIR="${ESPHOOK_INSTALL_DIR:-${HOME_DIR:+$HOME_DIR/.local/share/esphook}}"
BIN_DIR="${ESPHOOK_BIN_DIR:-${HOME_DIR:+$HOME_DIR/.local/bin}}"
TOOLS="all"
ASSUME_YES=0
NO_HOOKS=0

usage() {
  cat <<'EOF'
用法：
  install.sh [选项]

从 GitHub Release 下载最新 esphook 主机文件和 ESP32 固件。
默认会在下载、校验并安装主机文件后，询问是否写入 Agent Hook。

选项：
  --tools LIST       要安装的 Hook：all 或 claude,codex,kimi,cursor
  --no-hooks         只下载主机文件和固件，不安装 Hook
  --yes              跳过 Hook 确认（仅适合明确的自动化调用）
  --release TAG      下载指定 Release，默认 latest
  --install-dir DIR 主机文件和固件安装目录
  --bin-dir DIR      esphook 命令安装目录
  --help             显示帮助

环境变量：
  ESPHOOK_REPO       GitHub 仓库，默认 Ken-u/esphook
  ESPHOOK_API_URL    GitHub API 地址，默认 https://api.github.com
  ESPHOOK_INSTALL_DIR、ESPHOOK_BIN_DIR
EOF
}

die() {
  echo "esphook: $*" >&2
  exit 1
}

show_input_injection_status() {
  local tool=""
  local session="${XDG_SESSION_TYPE:-unknown}"
  local system_name="$(uname -s 2>/dev/null || echo unknown)"

  if [[ "$system_name" == "Darwin" ]]; then
    if command -v osascript >/dev/null 2>&1; then
      echo "esphook: 按键输入回传：已检测到 macOS 内置 osascript"
      echo "esphook: 首次使用请在 系统设置 > 隐私与安全性 > 辅助功能 中允许运行 daemon 的终端或应用"
    else
      echo "esphook: 警告：macOS 未找到系统自带的 osascript"
      echo "esphook: 当前只能使用提醒显示，板子按键输入回传不可用"
    fi
    return
  fi

  for candidate in ydotool xdotool wtype; do
    if command -v "$candidate" >/dev/null 2>&1; then
      tool="$candidate"
      break
    fi
  done

  if [[ -n "$tool" ]]; then
    echo "esphook: 按键输入回传：已检测到 $tool"
    if [[ "$tool" == "ydotool" ]] && ! command -v ydotoold >/dev/null 2>&1; then
      echo "esphook: 注意：ydotool 还需要运行 ydotoold，否则输入回传仍不可用"
    fi
    return
  fi

  echo "esphook: 警告：未检测到 ydotool、xdotool 或 wtype"
  echo "esphook: 通知显示和 daemon 不受影响，但板子按键输入回传当前不可用"
  case "$session" in
    x11)
      echo "esphook: X11 安装：sudo apt install xdotool"
      ;;
    wayland)
      echo "esphook: Wayland 安装：sudo apt install wtype"
      echo "esphook: 也可以安装 ydotool，但必须另外运行 ydotoold"
      ;;
    *)
      echo "esphook: X11 可安装 xdotool；Wayland 可安装 wtype"
      echo "esphook: 安装后请重启 daemon，输入回传才会生效"
      ;;
  esac
}

while (($# > 0)); do
  case "$1" in
    --tools)
      (($# >= 2)) || die "--tools 缺少参数"
      TOOLS="$2"
      shift 2
      ;;
    --no-hooks)
      NO_HOOKS=1
      shift
      ;;
    --yes)
      ASSUME_YES=1
      shift
      ;;
    --release)
      (($# >= 2)) || die "--release 缺少参数"
      RELEASE_REF="$2"
      shift 2
      ;;
    --install-dir)
      (($# >= 2)) || die "--install-dir 缺少参数"
      INSTALL_DIR="$2"
      shift 2
      ;;
    --bin-dir)
      (($# >= 2)) || die "--bin-dir 缺少参数"
      BIN_DIR="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      die "未知参数：$1（使用 --help 查看帮助）"
      ;;
  esac
done

if [[ -z "$HOME_DIR" && ( -z "$INSTALL_DIR" || -z "$BIN_DIR" ) ]]; then
  die "HOME 未设置，请显式传入 --install-dir 和 --bin-dir"
fi

for required_command in curl python3 tar; do
  command -v "$required_command" >/dev/null 2>&1 || \
    die "缺少依赖命令：$required_command"
done

case "$RELEASE_REF" in
  latest)
    release_endpoint="$API_BASE/repos/$REPOSITORY/releases/latest"
    ;;
  *)
    release_endpoint="$API_BASE/repos/$REPOSITORY/releases/tags/$RELEASE_REF"
    ;;
esac

temporary_dir="$(mktemp -d "${TMPDIR:-/tmp}/esphook-install.XXXXXX")"
cleanup() {
  rm -rf "$temporary_dir"
}
trap cleanup EXIT

echo "esphook: 查询 GitHub Release $REPOSITORY@$RELEASE_REF"
release_json="$(curl -fsSL --retry 3 --retry-delay 1 "$release_endpoint")" \
  || die "无法读取 GitHub Release；请检查网络或仓库是否已有 Release"

release_variables="$(printf '%s' "$release_json" | python3 -c '
import json
import shlex
import sys

data = json.load(sys.stdin)
tag = data.get("tag_name")
assets = {
    asset.get("name"): asset.get("browser_download_url")
    for asset in data.get("assets", [])
}
required = {
    "HOST_URL": "esphook-host.tar.gz",
    "FIRMWARE_URL": "esphook-firmware.bin",
    "FULL_FLASH_URL": "esphook-full-flash.zip",
    "CHECKSUMS_URL": "SHA256SUMS",
}
if not tag:
    raise SystemExit("Release 没有 tag_name")
missing = [name for name in required.values() if not assets.get(name)]
if missing:
    raise SystemExit("Release 缺少资产：" + ", ".join(missing))
print("RELEASE_TAG=" + shlex.quote(tag))
for variable, name in required.items():
    print(variable + "=" + shlex.quote(assets[name]))
' )" || die "无法解析 GitHub Release 信息"
eval "$release_variables"

host_archive="$temporary_dir/esphook-host.tar.gz"
firmware_file="$temporary_dir/esphook-firmware.bin"
full_flash_file="$temporary_dir/esphook-full-flash.zip"
checksums_file="$temporary_dir/SHA256SUMS"

download_asset() {
  local url="$1"
  local destination="$2"
  echo "esphook: 下载 $(basename "$destination")"
  curl -fL --retry 3 --retry-delay 1 --silent --show-error \
    -o "$destination" "$url"
}

download_asset "$HOST_URL" "$host_archive" || die "主机文件下载失败"
download_asset "$FIRMWARE_URL" "$firmware_file" || die "OTA 固件下载失败"
download_asset "$FULL_FLASH_URL" "$full_flash_file" || die "完整烧录包下载失败"
download_asset "$CHECKSUMS_URL" "$checksums_file" || die "SHA256 校验文件下载失败"

python3 - "$checksums_file" "$host_archive" "$firmware_file" "$full_flash_file" <<'PY'
import hashlib
from pathlib import Path
import sys

checksums_path = Path(sys.argv[1])
expected = {}
for line in checksums_path.read_text(encoding="utf-8").splitlines():
    line = line.strip()
    if not line:
        continue
    digest, name = line.split(maxsplit=1)
    expected[name.lstrip("*")] = digest.lower()

for filename in sys.argv[2:]:
    path = Path(filename)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    wanted = expected.get(path.name)
    if wanted is None:
        raise SystemExit(f"SHA256SUMS 中没有 {path.name}")
    if digest != wanted:
        raise SystemExit(f"SHA256 校验失败：{path.name}")
    print(f"esphook: SHA256 OK {path.name}")
PY

mkdir -p "$INSTALL_DIR" "$BIN_DIR"
INSTALL_DIR="$(CDPATH= cd -- "$INSTALL_DIR" && pwd)"
BIN_DIR="$(CDPATH= cd -- "$BIN_DIR" && pwd)"
release_dir="$INSTALL_DIR/releases/$RELEASE_TAG"
mkdir -p "$release_dir" "$release_dir/firmware"
tar -xzf "$host_archive" -C "$release_dir"
cp "$firmware_file" "$release_dir/firmware/esphook-firmware.bin"
cp "$full_flash_file" "$release_dir/firmware/esphook-full-flash.zip"
cp "$checksums_file" "$release_dir/firmware/SHA256SUMS"
printf '%s\n' "$RELEASE_TAG" > "$INSTALL_DIR/current"

launcher="$BIN_DIR/esphook"
cat > "$launcher" <<EOF
#!/usr/bin/env bash
exec "$release_dir/bin/esphook" "\$@"
EOF
chmod 755 "$launcher"

echo "esphook: 主机文件已安装到 $release_dir"
echo "esphook: OTA 固件：$release_dir/firmware/esphook-firmware.bin"
echo "esphook: 完整烧录包：$release_dir/firmware/esphook-full-flash.zip"
echo "esphook: 命令：$launcher"
show_input_injection_status

if ((NO_HOOKS)); then
  echo "esphook: 已按要求跳过 Hook 安装"
  exit 0
fi

install_hooks=0
if ((ASSUME_YES)); then
  install_hooks=1
elif [[ -r /dev/tty ]]; then
  echo ""
  echo "即将修改以下用户级 Agent 配置并安装 esphook Hook：$TOOLS"
  echo "已有配置会先保留 .esphook.bak 备份。"
  if IFS= read -r -p "确认安装 Hook？[y/N] " answer < /dev/tty; then
    case "$answer" in
      y|Y|yes|YES|Yes)
        install_hooks=1
        ;;
    esac
  fi
else
  echo "esphook: 当前没有可用终端，未安装 Hook；需要时重新运行并加 --yes"
fi

if ((install_hooks)); then
  python3 "$release_dir/host/install_hooks.py" --tools "$TOOLS"
else
  echo "esphook: 已跳过 Hook 安装"
fi
