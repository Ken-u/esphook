#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I framework/include/esphook tests/test_haptic_steer.c -lm
