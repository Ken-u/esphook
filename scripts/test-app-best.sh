#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-best-test.XXXXXX")"
trap 'rm -f "$test_bin"' EXIT
# The best-score store sits on app_storage_*; the test supplies a fake one, so no ESP-IDF is needed.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I framework/include -I framework/include/esphook \
  tests/test_app_best.c framework/core/app_best.c -o "$test_bin"
"$test_bin"
