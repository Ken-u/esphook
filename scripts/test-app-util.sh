#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-util-test.XXXXXX")"
trap 'rm -f "$test_bin"' EXIT
# The pure-maths/random/step kit is header-only: it must build with nothing but the C library.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I framework/include tests/test_app_util.c -o "$test_bin"
"$test_bin"
