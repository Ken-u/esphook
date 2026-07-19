idf_component_register(
    SRCS "main.c" "usb_cdc.c" "config_store.c" "display_model.c" "display_view.c"
         "net_wifi.c" "beeper.c" "events.c" "http_server.c"
         "input_task.c" "display_task.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_usb_serial_jtag esp_driver_spi esp_driver_gpio
             esp_driver_ledc esp_wifi esp_netif esp_event esp_http_server esp_http_client
             esp_lcd json
)
# unity 测试：编译进二进制时取消注释下行并打开 CONFIG_UNITY_ENABLE_IDF_TEST_RUNNER
# idf_component_register(APPEND SRCS "test/test_config_store.c" "test/test_display_model.c"
#                        REQUIRES unity)
