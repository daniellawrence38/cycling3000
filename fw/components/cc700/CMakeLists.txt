# The portable code, shared with the host build: core/, hal/ headers, ui/ screens, fonts and icons.
set(R ${CMAKE_CURRENT_LIST_DIR}/../../..)
file(GLOB CORE_SRC ${R}/core/src/*.c)
file(GLOB UI_SRC ${R}/ui/src/*.c)
file(GLOB FONT_SRC ${R}/ui/fonts/gen/*.c)
idf_component_register(
    SRCS ${CORE_SRC} ${UI_SRC} ${FONT_SRC} ${R}/ui/icons/icons.c
    INCLUDE_DIRS ${R}/core/include ${R}/hal ${R}/ui/src ${R}/ui/fonts ${R}/ui/icons
    REQUIRES lvgl__lvgl)
# Fonts and icons are generated; the rest builds warning-free on the host with -Wall -Wextra -Werror.
set_source_files_properties(${FONT_SRC} ${R}/ui/icons/icons.c PROPERTIES COMPILE_OPTIONS "-w")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-format-truncation)
