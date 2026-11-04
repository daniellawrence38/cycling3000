idf_component_register(
    SRCS main.c link_gatt.c sensors.c hal_esp.c hal_display_esp.c hal_i2c_esp.c hal_touch_esp.c ble_parse.c fs_null.c
    INCLUDE_DIRS .
    PRIV_REQUIRES cc700 lvgl__lvgl bt esp_timer esp_system esp_lcd esp_driver_gpio esp_driver_ledc esp_driver_i2c)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
