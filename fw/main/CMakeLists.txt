idf_component_register(
    SRCS main.c link_gatt.c sensors.c hal_esp.c ble_parse.c fs_null.c
    INCLUDE_DIRS .
    PRIV_REQUIRES cc700 lvgl__lvgl bt esp_timer esp_system)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
