cmake_minimum_required(VERSION 3.16)
project(cc700 C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(CC_SANITIZE "Build host code with ASan and UBSan" OFF)

add_compile_options(-Wall -Wextra -Werror -Wshadow -Wstrict-prototypes)
if(CC_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1)
  add_link_options(-fsanitize=address,undefined)
endif()

enable_testing()
add_subdirectory(core)
add_subdirectory(simunit)
# The UI needs LVGL (ui/deps.sh); built in the plain configuration only
if(NOT CC_SANITIZE AND EXISTS ${CMAKE_SOURCE_DIR}/build/deps/lvgl/lvgl.h)
  add_subdirectory(ui)
endif()
