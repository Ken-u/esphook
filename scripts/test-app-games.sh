#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
app_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-app-test.XXXXXX")"
trap 'rm -f "$app_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main tests/test_app_input.c main/app_input.c -o "$app_test_bin"
"$app_test_bin"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main -I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}" \
  tests/test_remote_game.c main/remote_game.c main/stock_chart.c main/robot_ui_protocol.c \
  "${CJSON_LIB:--lcjson}" -lm -o "$app_test_bin"
"$app_test_bin"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I tests/ui_stubs -I main -I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}" \
  tests/test_game_runtime.c main/game_runtime.c main/app_input.c main/basketball_game.c \
  main/remote_game.c main/robot_ui_protocol.c "${CJSON_LIB:--lcjson}" -lm -o "$app_test_bin"
"$app_test_bin"
