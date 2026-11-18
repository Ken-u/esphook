#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
# The screen tuning grammar is pure C: no ESP-IDF, no display.
ctest_run -I framework/include -I framework/include/esphook tests/test_app_screen.c framework/core/app_screen.c
