idf_component_register(SRCS probe.c PRIV_REQUIRES esp_driver_gpio esp_system)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
