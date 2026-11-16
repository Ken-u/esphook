#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${LVGL_SOURCE_DIR:?Point LVGL_SOURCE_DIR at a checkout of lvgl v9.5.0}"
app_ui_build="${APP_UI_BUILD_DIR:-${TMPDIR:-/tmp}/esphook-app-ui-build}"
app_ui_frames="${APP_UI_FRAMES_DIR:-$app_ui_build/frames}"
mkdir -p "$app_ui_frames"
app_ui_args=()
if [[ -n "${CJSON_INCLUDE_DIR:-}" ]]; then app_ui_args+=("-DCJSON_INCLUDE=$CJSON_INCLUDE_DIR"); fi
cmake -S tests/headless -B "$app_ui_build" -DLVGL_SOURCE_DIR="$LVGL_SOURCE_DIR" \
    "${app_ui_args[@]}"
cmake --build "$app_ui_build" -j "${APP_UI_BUILD_JOBS:-4}"
ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" UBSAN_OPTIONS=halt_on_error=1 \
    "$app_ui_build/apps_headless" "$app_ui_frames"
