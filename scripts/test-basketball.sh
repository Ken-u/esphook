#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware tests/test_basketball.c apps/basketball/firmware/basketball_game.c framework/core/app_input.c -lm
