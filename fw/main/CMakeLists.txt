idf_component_register(
    SRCS main.c link_gatt.c sensors.c hal_esp.c hal_display_esp.c hal_i2c_esp.c hal_touch_esp.c hal_nrf_esp.c nrf_proto.c hal_gnss_esp.c nmea.c ble_parse.c fs_null.c hal_fs_esp.c fat_probe.c dump_fmt.c emmc_dump_esp.c
    INCLUDE_DIRS .
    PRIV_REQUIRES cc700 lvgl__lvgl bt esp_timer esp_system esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_uart fatfs sdmmc esp_driver_sdmmc esp_driver_usb_serial_jtag)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
