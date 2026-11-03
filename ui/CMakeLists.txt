# UI: LVGL (fetched by ui/deps.sh into build/deps/lvgl), the screens, and the ccsim simulator.
set(LVGL_DIR ${CMAKE_SOURCE_DIR}/build/deps/lvgl)
file(GLOB_RECURSE LVGL_SRC CONFIGURE_DEPENDS ${LVGL_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})
target_include_directories(lvgl PUBLIC ${LVGL_DIR} ${LVGL_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)
target_compile_options(lvgl PRIVATE -w)

find_package(PkgConfig)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(SDL2 sdl2)
endif()
if(SDL2_FOUND)
  target_compile_definitions(lvgl PUBLIC CC_LV_USE_SDL=1)
  target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
  target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})
endif()

file(GLOB UI_FONTS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fonts/gen/*.c)
add_library(cc700fonts STATIC ${UI_FONTS} icons/icons.c)
target_include_directories(cc700fonts PUBLIC fonts icons)
target_link_libraries(cc700fonts PUBLIC lvgl)
target_compile_options(cc700fonts PRIVATE -w)

file(GLOB UI_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_library(cc700ui STATIC ${UI_SRC})
target_include_directories(cc700ui PUBLIC src)
target_link_libraries(cc700ui PUBLIC cc700core cc700fonts lvgl)
# snprintf into fixed display buffers: truncation is safe there
target_compile_options(cc700ui PRIVATE -Wno-format-truncation)

add_library(lodepng STATIC sim/lodepng/lodepng.c)
target_include_directories(lodepng PUBLIC sim)
target_compile_options(lodepng PRIVATE -w)

add_executable(ccsim sim/ccsim.c sim/app.c sim/lint.c ${CMAKE_SOURCE_DIR}/simunit/dirfs.c ${CMAKE_SOURCE_DIR}/simunit/player.c)
target_include_directories(ccsim PRIVATE sim ${CMAKE_SOURCE_DIR}/simunit)
target_link_libraries(ccsim cc700ui lodepng)
