#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/fortune/firmware tests/test_fortune.c apps/fortune/firmware/fortune_core.c
