cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Bluepad32 liegt als Git-Submodul unter external/bluepad32
set(BLUEPAD32_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/external/bluepad32)
set(ENV{BTSTACK_ROOT} ${BLUEPAD32_ROOT}/external/btstack)

set(EXTRA_COMPONENT_DIRS ${BLUEPAD32_ROOT}/src/components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
include($ENV{BTSTACK_ROOT}/port/esp32/components/btstack/cmake/BTstack_project.cmake)

# Keine Pfade der Build-Umgebung ins Image schreiben (__FILE__ in Bluepad32/BTstack)
get_filename_component(BLUEPAD32_REAL ${BLUEPAD32_ROOT} REALPATH)
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${BLUEPAD32_REAL}=bluepad32" APPEND)
idf_build_set_property(COMPILE_OPTIONS "-fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=." APPEND)

project(esp32_pageflippy)
