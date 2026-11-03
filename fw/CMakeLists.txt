# CC700 firmware skeleton (brief WP7). ESP-IDF version pinned in IDF_VERSION. Build with `make fw-build`
# (app only: the unit keeps its own bootloader and partition table, which are never flashed from here).
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/components)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cc700)
