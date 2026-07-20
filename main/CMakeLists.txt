idf_component_register(
    SRCS "main.c" "usb_cdc.c" "config_store.c" "display_model.c" "display_view.c"
         "aihook_font_12.c" "aihook_font_14.c" "ota_update.c"
         "font_store.c"
         "device_link.c"
         "net_wifi.c" "beeper.c" "events.c" "http_server.c"
         "input_task.c" "display_task.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_usb_serial_jtag esp_driver_spi esp_driver_gpio
             esp_driver_ledc esp_wifi esp_netif esp_event esp_http_server esp_http_client
             esp_lcd esp_timer lvgl json app_update spi_flash
             mbedtls lwip
)

# 字库不是 OTA 内容，只有完整 USB flash 才会更新这个固定数据分区。
esptool_py_flash_to_partition(flash "fontdata" ${PROJECT_DIR}/main/fontdata.bin)
# unity 测试：编译进二进制时取消注释下行并打开 CONFIG_UNITY_ENABLE_IDF_TEST_RUNNER
# idf_component_register(APPEND SRCS "test/test_config_store.c" "test/test_display_model.c"
#                        REQUIRES unity)
