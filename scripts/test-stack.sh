#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-stack-test.XXXXXX")"
trap 'rm -f "$test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I framework/include -I apps/zombie/firmware -I apps/racing/firmware -I apps/starship/firmware \
  tests/test_stack.c apps/zombie/firmware/zombie_models.c apps/racing/firmware/racing_models.c apps/starship/firmware/starship_models.c -o "$test_bin"
"$test_bin"
