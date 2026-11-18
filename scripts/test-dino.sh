#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I apps/dino/firmware tests/test_dino.c apps/dino/firmware/dino_game.c -lm
