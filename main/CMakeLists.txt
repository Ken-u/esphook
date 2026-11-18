# IDF first includes this file from a script-mode dependency scan. Catalog
# generation and CONFIGURE_DEPENDS belong to the normal configure phase only.
if(NOT CMAKE_SCRIPT_MODE_FILE)
    include("${PROJECT_DIR}/cmake/apps.cmake")
endif()
set(framework "${PROJECT_DIR}/framework")
idf_component_register(
    SRCS "main.c" "usb_cdc.c" "usb_cdc_tx.c" "config_store.c"
         "aihook_font_12.c" "aihook_font_16.c" "ota_update.c"
         "font_store.c"
         "device_link.c" "http_post.c"
         "net_wifi.c" "net_time.c" "beeper.c" "events.c" "http_server.c"
         "input_task.c" "display_task.c"
         "${framework}/core/app_input.c" "${framework}/core/app_manager.c" "${framework}/core/app_protocol.c"
         "${framework}/core/app_keys.c" "${framework}/core/app_time.c" "${framework}/core/app_alarm.c" "${framework}/core/app_best.c" "${framework}/core/app_screen.c"
         "${framework}/core/app_hardware.c" "${framework}/ports/esp32/hardware_service.c"
         "${framework}/ports/esp32/app_runtime.c" "${framework}/ports/esp32/app_services.c"
         "${framework}/ports/esp32/app_ui.c" "${framework}/ports/esp32/display_port.c"
         "${framework}/services/hook_model.c" "${framework}/services/motion_service.c"
         "${framework}/services/robot_follow.c" "${framework}/services/display_service.c"
         "${framework}/services/display_protocol.c" "${framework}/services/radar_plot.c"
         "${framework}/drivers/ld2450.c" "${framework}/drivers/radar_ld2450.c" "${framework}/drivers/servo_nhy009.c"
         ${ESPHOOK_APP_SOURCES}
    INCLUDE_DIRS "." "${framework}/include" "${framework}/include/esphook" ${ESPHOOK_APP_INCLUDES}
    REQUIRES nvs_flash esp_driver_usb_serial_jtag esp_driver_spi esp_driver_gpio
             esp_driver_ledc esp_driver_uart esp_wifi esp_netif esp_event esp_http_server
             esp_lcd esp_timer lvgl json app_update spi_flash
             mbedtls lwip esp_hw_support esp_app_format esp_system
)

# 字库不是 OTA 内容，只有完整 USB flash 才会更新这个固定数据分区。
esptool_py_flash_to_partition(flash "fontdata" ${PROJECT_DIR}/main/fontdata.bin)
# unity 测试：编译进二进制时取消注释下行并打开 CONFIG_UNITY_ENABLE_IDF_TEST_RUNNER
# idf_component_register(APPEND SRCS "test/test_config_store.c" "test/test_display_model.c"
#                        REQUIRES unity)
