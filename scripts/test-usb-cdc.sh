#!/usr/bin/env bash
set -euo pipefail
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1}"
cd "$(dirname "$0")/.."
cdc_test_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-cdc.XXXXXX")"
trap 'rm -f "$cdc_test_bin"' EXIT
# The ESP-IDF USB Serial/JTAG driver is simulated; this covers framing and loss reporting only.
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I main tests/test_usb_cdc_tx.c main/usb_cdc_tx.c -o "$cdc_test_bin"
"$cdc_test_bin"
