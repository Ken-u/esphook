#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
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
#include "esphook/app_screen.h"
EOF
cjson=(-I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}")
cjson_lib="${CJSON_LIB:--lcjson}"
ctest_run -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware tests/test_app_input.c framework/core/app_input.c
ctest_run -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware "${cjson[@]}" \
  tests/test_remote_game.c apps/stock/firmware/remote_game.c apps/stock/firmware/stock_chart.c framework/services/display_protocol.c "$cjson_lib" -lm
ctest_run -I framework/include/esphook "${cjson[@]}" \
  tests/test_app_framework.c framework/core/app_manager.c framework/core/app_input.c framework/core/app_protocol.c framework/core/app_keys.c \
  framework/core/app_time.c framework/core/app_alarm.c framework/core/app_hardware.c framework/services/display_protocol.c "$cjson_lib" -lm
ctest_run -I framework/include/esphook \
  tests/test_app_keys.c framework/core/app_keys.c framework/core/app_manager.c framework/core/app_input.c framework/core/app_alarm.c framework/core/app_time.c
