#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
# The ESP-IDF USB Serial/JTAG driver is simulated; this covers framing and loss reporting only.
ctest_run -I main tests/test_usb_cdc_tx.c main/usb_cdc_tx.c
