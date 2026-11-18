#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
# The best-score store sits on app_storage_*; the test supplies a fake one, so no ESP-IDF is needed.
ctest_run -I framework/include -I framework/include/esphook tests/test_app_best.c framework/core/app_best.c
