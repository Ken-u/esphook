#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
robot_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-robot-test.XXXXXX")"
trap 'rm -f "$robot_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware tests/test_robot.c framework/drivers/ld2450.c framework/services/robot_follow.c -lm -o "$robot_test_bin"
"$robot_test_bin"
