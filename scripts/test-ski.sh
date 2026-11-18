#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -O1 -I apps/ski/firmware tests/test_ski.c apps/ski/firmware/ski_game.c -lm
