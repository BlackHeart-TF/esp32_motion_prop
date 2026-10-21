cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(halloween_scare)

esptool_py_flash_to_partition(flash "screams" "${CMAKE_BINARY_DIR}/screams.bin")
add_dependencies(flash screams_bin)
