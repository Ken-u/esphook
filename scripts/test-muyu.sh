#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
muyu_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-muyu-test.XXXXXX")"
trap 'rm -f "$muyu_test_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I apps/muyu/firmware tests/test_muyu.c apps/muyu/firmware/muyu_core.c -o "$muyu_test_bin"
"$muyu_test_bin"
