#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I framework/include -I apps/zombie/firmware -I apps/racing/firmware -I apps/starship/firmware tests/test_stack.c apps/zombie/firmware/zombie_models.c apps/racing/firmware/racing_models.c apps/starship/firmware/starship_models.c
