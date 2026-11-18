#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I apps/alarm/firmware tests/test_alarm.c apps/alarm/firmware/alarm_core.c framework/core/app_alarm.c framework/core/app_time.c -I framework/include/esphook -I framework/include
