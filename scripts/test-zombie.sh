#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/zombie/firmware tests/test_zombie.c apps/zombie/firmware/zombie_core.c
