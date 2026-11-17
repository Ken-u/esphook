#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
dino_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-dino-test.XXXXXX")"
trap 'rm -f "$dino_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I apps/dino/firmware tests/test_dino.c apps/dino/firmware/dino_game.c -lm -o "$dino_test_bin"
"$dino_test_bin"
