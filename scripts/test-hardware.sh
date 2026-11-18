#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_build -I tests/ui_stubs -I framework/include/esphook -I main \
  -DCONFIG_ROBOT_RADAR_RX_GPIO=20 -DCONFIG_ROBOT_RADAR_TX_GPIO=21 -DCONFIG_ROBOT_RADAR_BAUD=256000 \
  -DCONFIG_ROBOT_SERVO_GPIO=10 -DCONFIG_ROBOT_SERVO_HZ=50 \
  tests/test_hardware_boot.c framework/services/motion_service.c framework/services/robot_follow.c framework/core/app_hardware.c -lm
for profile in "auto" "screen" "robot" "auto radar" "robot radar" "robot servo"; do
  # shellcheck disable=SC2086
  "$CTEST_BIN" $profile
done
