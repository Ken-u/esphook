#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-alarm-test.XXXXXX")"
trap 'rm -f "$test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I apps/alarm/firmware tests/test_alarm.c apps/alarm/firmware/alarm_core.c framework/core/app_alarm.c framework/core/app_time.c -I framework/include/esphook -I framework/include -o "$test_bin"
"$test_bin"
