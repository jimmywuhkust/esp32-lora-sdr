# Add this folder to EXTRA_COMPONENT_DIRS for full native half-duplex RX/TX.
if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "Native LoRa SDR is verified only on ESP32-S3")
endif()
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND (NOT CONFIG_FREERTOS_UNICORE OR NOT CONFIG_SPIRAM))
    message(FATAL_ERROR "Use the supplied native sdkconfig: unicore RTOS + OPI PSRAM required")
endif()
set(root "${CMAKE_CURRENT_LIST_DIR}/..")
set(backend "${root}/firmware/iq-capture/main")
idf_component_register(
    SRCS ${root}/src/LoRaSDR.cpp ${root}/src/ESP32S3Radio.cpp
         ${root}/src/PacketDecoder.cpp ${root}/src/IQDecoder.cpp ${root}/src/NativeReceive.cpp
         ${backend}/targets/esp32s3/receiver.c ${backend}/common/ring_capture.c
         ${backend}/common/burst_serial.c ${backend}/common/spectrum_stats.c
         ${backend}/common/rx_recalibration.c ${backend}/common/lora_chirp.c
         ${backend}/common/native_packet.cpp
         ${backend}/targets/esp32s3/s3_fft_rnd.S ${backend}/targets/esp32s3/s3_unpack.S
         ${backend}/targets/esp32s3/s3_core1.S ${backend}/targets/esp32s3/s3_firdot.S
    INCLUDE_DIRS ${root}/src
    PRIV_INCLUDE_DIRS ${backend}/common ${backend}/targets/esp32s3
    REQUIRES esp_phy esp_wifi nvs_flash esp_timer esp_driver_uart esp_driver_usb_serial_jtag esp-dsp
    LDFRAGMENTS linker.lf)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LORA_SDR_NO_APP_MAIN=1 IQ_CAPTURE_PSRAM_QUEUE=1)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-T${backend}/targets/esp32s3/sram_guard.ld" "-Wl,--wrap=chip_v7_set_chan_ana")
