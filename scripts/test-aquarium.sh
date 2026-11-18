#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/aquarium/firmware tests/test_aquarium.c apps/aquarium/firmware/aquarium_core.c
