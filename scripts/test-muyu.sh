#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I apps/muyu/firmware tests/test_muyu.c apps/muyu/firmware/muyu_core.c
