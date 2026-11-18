#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/starship/firmware tests/test_starship.c apps/starship/firmware/starship_core.c
