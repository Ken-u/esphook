#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-starship-test.XXXXXX")"
trap 'rm -f "$test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I apps/starship/firmware tests/test_starship.c apps/starship/firmware/starship_core.c -o "$test_bin"
"$test_bin"
