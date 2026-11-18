#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include/esphook tests/test_app_time.c framework/core/app_time.c
