#!/usr/bin/env bash
# Every host check in one place. CI (.github/workflows/ci.yml, job "Host tests") runs exactly this script, so a green
# local run is a green CI run for the host side.
#
#   scripts/check-all.sh                 run everything
#   scripts/check-all.sh racing zombie   only the checks whose name contains one of the words
#   scripts/check-all.sh --list          print the check names
#
# Environment
#   CJSON_INCLUDE_DIR, CJSON_LIB   cJSON for the protocol tests (Homebrew is found by itself; Debian: libcjson-dev)
#   LVGL_SOURCE_DIR                a checkout of lvgl v9.5.0 for the render test (default /tmp/esphook-lvgl when it exists)
#   APP_UI_BUILD_DIR, APP_UI_FRAMES_DIR   where the render test builds and writes its frames
#   PYTHON                         interpreter for the PC tests (default python3)
#   CHECK_STRICT=1                 a check that cannot run here (no LVGL, no Pillow) fails instead of being skipped
set -uo pipefail
cd "$(dirname "$0")/.."

PYTHON="${PYTHON:-python3}"
if [[ -z "${CJSON_INCLUDE_DIR:-}" ]] && command -v brew >/dev/null 2>&1; then
    cjson_prefix="$(brew --prefix cjson 2>/dev/null || true)"
    if [[ -n "$cjson_prefix" && -d "$cjson_prefix/include/cjson" ]]; then
        export CJSON_INCLUDE_DIR="$cjson_prefix/include/cjson" CJSON_LIB="$cjson_prefix/lib/libcjson.dylib"
    fi
fi
if [[ -z "${LVGL_SOURCE_DIR:-}" && -d /tmp/esphook-lvgl ]]; then export LVGL_SOURCE_DIR=/tmp/esphook-lvgl; fi

mode=run
filters=()
for arg in "$@"; do
    case "$arg" in
        --list) mode=list ;;
        -h|--help) sed -n '2,16p' "$0"; exit 0 ;;
        *) filters+=("$arg") ;;
    esac
done

log="$(mktemp "${TMPDIR:-/tmp}/esphook-check.XXXXXX")"
trap 'rm -f "$log"' EXIT
failed=()
skipped=()
passed=0
in_ci="${GITHUB_ACTIONS:-}"

wanted() {
    [[ ${#filters[@]} -eq 0 ]] && return 0
    local f
    for f in "${filters[@]}"; do [[ "$1" == *"$f"* ]] && return 0; done
    return 1
}

# check <name> <command...>   exit status 77 from the command means "cannot run here" (skipped, or failed under CHECK_STRICT)
check() {
    local name="$1"
    shift
    wanted "$name" || return 0
    if [[ $mode == list ]]; then echo "$name"; return 0; fi
    local start=$SECONDS status=0
    [[ -n $in_ci ]] && echo "::group::$name"
    "$@" >"$log" 2>&1 || status=$?
    if [[ $status -eq 0 ]]; then
        echo "PASS  $name ($((SECONDS - start))s)"
        passed=$((passed + 1))
    elif [[ $status -eq 77 && -z "${CHECK_STRICT:-}" ]]; then
        echo "SKIP  $name ($(tail -1 "$log"))"
        skipped+=("$name")
    else
        echo "FAIL  $name (exit $status)"
        if [[ -n $in_ci ]]; then cat "$log"; else tail -40 "$log"; fi
        failed+=("$name")
    fi
    [[ -n $in_ci ]] && echo "::endgroup::"
    return 0
}
need() { echo "$1" >&2; return 77; }
py() { "$PYTHON" "$@"; }
c_tests() { local t; for t in "$@"; do check "test-$t" "./scripts/test-$t.sh"; done; }

ui_render() {
    [[ -n "${LVGL_SOURCE_DIR:-}" ]] || { need "set LVGL_SOURCE_DIR to a checkout of lvgl v9.5.0"; return; }
    ./scripts/test-app-ui.sh
}
mirror_pc() {
    "$PYTHON" -c 'import PIL' 2>/dev/null || { need "Pillow is not installed"; return; }
    "$PYTHON" apps/mirror/pc/test_mirror.py
}
installers() {
    bash -n install.sh && bash -n scripts/package-release.sh && bash -n scripts/stock-t.sh && ./install.sh --help >/dev/null
}
hooks_dry_run() {
    local home
    home="$(mktemp -d "${TMPDIR:-/tmp}/esphook-hook-dry-run.XXXXXX")"
    ./scripts/install-hooks.sh --tools all --dry-run --home "$home"
    local status=$?
    rm -rf "$home"
    return $status
}

check compileall "$PYTHON" -m compileall -q apps framework/pc host scripts tests
check check-apps py scripts/check-apps.py
check scaffold py tests/test_scaffold.py
check app-sdk py tests/test_app_sdk.py
check cmake-scan cmake -P tests/test_cmake_scan.cmake
check release-package py tests/test_release_package.py
check size-report py tests/test_size_report.py
check fontdata py tests/test_fontdata.py
check stock-pc py apps/stock/pc/test_stock_t.py
check hook-pc py apps/hook/pc/test_hooks.py
check device-link py apps/hook/pc/test_device_link.py
c_tests robot basketball dino ski muyu haptic-steer aquarium fishing starship racing app-time app-util app-best app-screen \
    fortune app-alarm alarm miner jump zombie stack mirror
check mirror-pc mirror_pc
c_tests hardware robot-ui app-games usb-cdc http-post
check key-bridge py apps/key_report/pc/test_bridge.py
check app-ui ui_render
check hooks-installer hooks_dry_run
check installers installers
check stock-t-demo ./scripts/stock-t.sh --demo --dry-run
check demo-pc py apps/demo/pc/main.py --dry-run
check alarm-pc py apps/alarm/pc/main.py --dry-run
check screentest-pc py apps/screentest/pc/main.py --dry-run plain
check key-pc py apps/key_report/pc/main.py --dry-run
check key-bridge-dry py apps/key_report/pc/bridge.py --dry-run
check diff-check git diff --check

[[ $mode == list ]] && exit 0
echo
if [[ ${#failed[@]} -eq 0 ]]; then
    if [[ ${#skipped[@]} -eq 0 ]]; then
        echo "ALL GREEN ($passed passed)"
    else
        echo "NO FAILURES, BUT NOT A FULL RUN ($passed passed, ${#skipped[@]} skipped: ${skipped[*]})"
    fi
    exit 0
fi
echo "FAILED: ${failed[*]}"
exit 1
