idf_component_register(
    SRCS main.c link_gatt.c sensors.c hal_esp.c hal_display_esp.c hal_i2c_esp.c hal_touch_esp.c hal_nrf_esp.c nrf_proto.c hal_gnss_esp.c nmea.c ble_parse.c fs_null.c hal_fs_esp.c fat_probe.c dump_fmt.c emmc_dump_esp.c mem_esp.c log_file_esp.c
    INCLUDE_DIRS .
    PRIV_REQUIRES cc700 lvgl__lvgl bt esp_timer esp_system esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c esp_driver_uart fatfs sdmmc esp_driver_sdmmc esp_driver_usb_serial_jtag)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
# make fw-export builds the /CC700 read-out with -DCC700_FS_EMMC_MODE=4 (board_pins.h).
if(DEFINED CC700_FS_EMMC_MODE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FS_EMMC_MODE=${CC700_FS_EMMC_MODE})
endif()
# LVGL's allocator lives in mem_esp.c (CONFIG_LV_USE_CUSTOM_MALLOC). liblvgl.a is linked after this
# component and nothing here calls lv_malloc_core, so without this the archive member that defines it
# might never be pulled in and the link would fail with undefined lv_*_core symbols.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")
