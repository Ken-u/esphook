#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware tests/test_robot.c framework/drivers/ld2450.c framework/services/robot_follow.c -lm
