idf_component_register(
    SRCS "main.c" "usb_cdc.c" "config_store.c" "test/test_config_store.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_driver_usb_serial_jtag unity
)
