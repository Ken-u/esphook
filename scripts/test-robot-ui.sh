#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
robot_ui_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-ui-test.XXXXXX")"
trap 'rm -f "$robot_ui_test_bin"' EXIT
# Debian/Ubuntu: libcjson-dev. Override include/library for another local SDK.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware -I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}" \
  tests/test_robot_ui.c framework/services/display_protocol.c framework/services/radar_plot.c \
  "${CJSON_LIB:--lcjson}" -lm -o "$robot_ui_test_bin"
"$robot_ui_test_bin"

"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I tests/ui_stubs -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware -I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}" \
  tests/test_robot_ui_state.c framework/services/display_service.c framework/services/display_protocol.c \
  "${CJSON_LIB:--lcjson}" -lm -o "$robot_ui_test_bin"
"$robot_ui_test_bin"
