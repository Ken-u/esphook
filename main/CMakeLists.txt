idf_component_register(
    SRCS "main.c" "usb_cdc.c" "config_store.c" "display_model.c" "display_view.c"
         "net_wifi.c" "beeper.c"
         "test/test_config_store.c" "test/test_display_model.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_usb_serial_jtag esp_driver_spi esp_driver_gpio
             esp_driver_ledc esp_wifi esp_netif esp_event unity
)
