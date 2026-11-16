#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
hardware_bin="$(mktemp "${TMPDIR:-/tmp}/esphook-hardware.XXXXXX")"
trap 'rm -f "$hardware_bin"' EXIT
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -pedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I tests/ui_stubs -I framework/include/esphook -I main \
  -DCONFIG_ROBOT_RADAR_RX_GPIO=20 -DCONFIG_ROBOT_RADAR_TX_GPIO=21 -DCONFIG_ROBOT_RADAR_BAUD=256000 \
  -DCONFIG_ROBOT_SERVO_GPIO=10 -DCONFIG_ROBOT_SERVO_HZ=50 \
  tests/test_hardware_boot.c framework/services/motion_service.c framework/services/robot_follow.c framework/core/app_hardware.c -lm -o "$hardware_bin"
"$hardware_bin" auto
"$hardware_bin" screen
"$hardware_bin" robot
"$hardware_bin" auto radar
"$hardware_bin" robot radar
"$hardware_bin" robot servo
