# For more information about build system see
# https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/build-system.html
# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)
# Firmware version: YYYYMMDDHH (build stamp). Override before project() if needed.
if(NOT DEFINED PROJECT_VER)
    set(PROJECT_VER "2026080916")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_options("-Wno-format")
project(FactoryProgram)
