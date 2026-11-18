#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/racing/firmware tests/test_racing.c apps/racing/firmware/racing_core.c
