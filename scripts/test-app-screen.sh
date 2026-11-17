#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-screen-test.XXXXXX")"
trap 'rm -f "$test_bin"' EXIT
# The screen tuning grammar is pure C: no ESP-IDF, no display.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I framework/include -I framework/include/esphook \
  tests/test_app_screen.c framework/core/app_screen.c -o "$test_bin"
"$test_bin"
