#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
# Debian/Ubuntu: libcjson-dev. Override include/library for another local SDK.
cjson=(-I "${CJSON_INCLUDE_DIR:-/usr/include/cjson}")
cjson_lib="${CJSON_LIB:--lcjson}"
ctest_run -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware "${cjson[@]}" \
  tests/test_robot_ui.c framework/services/display_protocol.c framework/services/radar_plot.c "$cjson_lib" -lm
ctest_run -I tests/ui_stubs -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware "${cjson[@]}" \
  tests/test_robot_ui_state.c framework/services/display_service.c framework/services/display_protocol.c "$cjson_lib" -lm
