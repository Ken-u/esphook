#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/miner/firmware tests/test_miner.c apps/miner/firmware/miner_core.c
