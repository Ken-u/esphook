#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I apps/mirror/firmware -I tests tests/test_mirror.c apps/mirror/firmware/mirror_codec.c
