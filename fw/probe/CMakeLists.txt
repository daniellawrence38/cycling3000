# fw/probe: a RAM-only image for hardware identification (brief WP7, BRINGUP.md step 3). Built as
# ESP-IDF's RAM app (CONFIG_APP_BUILD_TYPE_RAM, without flash support), loaded with
# `esptool --no-stub load-ram`; nothing is written to flash. ESP32-S3 support for this build type is
# listed and CI-tested in IDF v5.5.6 (tools/test_apps/system/ram_loadable_app).
cmake_minimum_required(VERSION 3.16)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cc700_probe)
