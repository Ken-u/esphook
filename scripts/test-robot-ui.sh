#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
robot_ui_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-ui-test.XXXXXX")"
trap 'rm -f "$robot_ui_test_bin"' EXIT
# Debian/Ubuntu: libcjson-dev. Override include/library for another local SDK.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main -I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}" \
  tests/test_robot_ui.c main/robot_ui_protocol.c main/radar_view.c \
  "${CJSON_LIB:--lcjson}" -lm -o "$robot_ui_test_bin"
"$robot_ui_test_bin"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I tests/ui_stubs -I main -I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}" \
  tests/test_robot_ui_state.c main/robot_ui.c main/robot_ui_protocol.c \
  "${CJSON_LIB:--lcjson}" -lm -o "$robot_ui_test_bin"
"$robot_ui_test_bin"
