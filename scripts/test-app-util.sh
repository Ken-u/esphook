#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
# The pure-maths/random/step kit is header-only: it must build with nothing but the C library.
ctest_run -I framework/include tests/test_app_util.c
