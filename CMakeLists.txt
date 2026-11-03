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
