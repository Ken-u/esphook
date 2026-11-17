#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
ski_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-ski-test.XXXXXX")"
trap 'rm -f "$ski_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g -O1 \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I apps/ski/firmware tests/test_ski.c apps/ski/firmware/ski_game.c -lm -o "$ski_test_bin"
"$ski_test_bin"
