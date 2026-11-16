#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
basketball_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-basketball-test.XXXXXX")"
trap 'rm -f "$basketball_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main tests/test_basketball.c main/basketball_game.c main/app_input.c -lm -o "$basketball_test_bin"
"$basketball_test_bin"
