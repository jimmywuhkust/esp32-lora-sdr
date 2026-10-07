cmake_minimum_required(VERSION 3.16)
set(ENV{IDF_COMPONENT_MANAGER} "0")
list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../idf"
    "${CMAKE_CURRENT_LIST_DIR}/../NativeDuplex/components/esp-dsp")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/../../firmware/iq-capture/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/../../firmware/iq-capture/sdkconfig.psram;${CMAKE_CURRENT_LIST_DIR}/arduino.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
set(COMPONENTS main __pio_env esp_https_ota esp_http_client unity)
project(arduino_lora_duplex)
# The radio component supplies the pinned SDK's RF helper archive and SRAM guard.
