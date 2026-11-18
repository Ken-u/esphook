#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
# The buzzer's sounds are plain tables (no ESP-IDF); the LEDC timer/channel plan is checked next to them.
ctest_run -I framework/include -I framework/include/esphook tests/test_app_sound.c framework/core/app_sound.c
