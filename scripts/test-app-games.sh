#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
app_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-app-test.XXXXXX")"
trap 'rm -f "$app_test_bin"' EXIT
# Public application/service contracts compile without ESP-IDF or RTOS headers.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -fsyntax-only -x c \
  -I framework/include -I framework/include/esphook - <<'EOF'
#include "esphook/app.h"
#include "esphook/app_services.h"
#include "esphook/app_math.h"
#include "esphook/app_rand.h"
#include "esphook/app_sync.h"
#include "esphook/app_command.h"
#include "esphook/app_best.h"
EOF
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware tests/test_app_input.c framework/core/app_input.c -o "$app_test_bin"
"$app_test_bin"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware -I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}" \
  tests/test_remote_game.c apps/stock/firmware/remote_game.c apps/stock/firmware/stock_chart.c framework/services/display_protocol.c \
  "${CJSON_LIB:--lcjson}" -lm -o "$app_test_bin"
"$app_test_bin"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I framework/include/esphook -I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}" \
  tests/test_app_framework.c framework/core/app_manager.c framework/core/app_input.c \
  framework/core/app_protocol.c framework/core/app_keys.c framework/core/app_time.c framework/core/app_alarm.c framework/core/app_hardware.c framework/services/display_protocol.c "${CJSON_LIB:--lcjson}" -lm -o "$app_test_bin"
"$app_test_bin"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer -I framework/include/esphook \
  tests/test_app_keys.c framework/core/app_keys.c framework/core/app_manager.c framework/core/app_input.c framework/core/app_alarm.c framework/core/app_time.c -o "$app_test_bin"
"$app_test_bin"
