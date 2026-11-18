#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/jump/firmware tests/test_jump.c apps/jump/firmware/jump_core.c
