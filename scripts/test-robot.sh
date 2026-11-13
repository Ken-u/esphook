#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
robot_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-robot-test.XXXXXX")"
trap 'rm -f "$robot_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main tests/test_robot.c main/ld2450.c main/robot_follow.c -lm -o "$robot_test_bin"
"$robot_test_bin"
