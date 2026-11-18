#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/fishing/firmware tests/test_fishing.c apps/fishing/firmware/fishing_core.c
