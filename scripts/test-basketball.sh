#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
basketball_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-basketball-test.XXXXXX")"
trap 'rm -f "$basketball_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main -I framework/include/esphook -I apps/basketball/firmware -I apps/stock/firmware tests/test_basketball.c apps/basketball/firmware/basketball_game.c framework/core/app_input.c -lm -o "$basketball_test_bin"
"$basketball_test_bin"
