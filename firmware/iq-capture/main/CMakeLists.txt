if(NOT IDF_TARGET STREQUAL "esp32s3")
    message(FATAL_ERROR "This capture backend was verified only on ESP32-S3")
endif()
option(IQ_CAPTURE_PSRAM_QUEUE "Experimental output queue for XIAO S3 N8R8" OFF)
if(IQ_CAPTURE_PSRAM_QUEUE AND NOT CONFIG_SPIRAM)
    message(FATAL_ERROR "PSRAM queue needs sdkconfig.psram and verified OPI PSRAM")
endif()
set(target_dir targets/esp32s3)
set(lora_src "${CMAKE_CURRENT_LIST_DIR}/../../../src")
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/native/PacketDecoder.cpp")
    set(lora_src "${CMAKE_CURRENT_LIST_DIR}/native")
endif()
idf_component_register(
    SRCS ${target_dir}/receiver.c
         common/burst_serial.c common/spectrum_stats.c common/rx_recalibration.c
         common/ring_capture.c common/lora_chirp.c common/native_packet.cpp
         ${lora_src}/LoRaSDR.cpp ${lora_src}/PacketDecoder.cpp ${lora_src}/IQDecoder.cpp ${lora_src}/ESP32S3Radio.cpp ${lora_src}/NativeReceive.cpp
         ${target_dir}/s3_fft_rnd.S ${target_dir}/s3_unpack.S
         ${target_dir}/s3_core1.S ${target_dir}/s3_firdot.S
    LDFRAGMENTS ${target_dir}/linker.lf
    PRIV_INCLUDE_DIRS common ${target_dir} ${lora_src}
    REQUIRES esp_phy esp_wifi nvs_flash esp_timer esp_driver_uart
             esp_driver_usb_serial_jtag esp-dsp)
target_link_options(${COMPONENT_LIB} INTERFACE
    "-T${CMAKE_CURRENT_LIST_DIR}/${target_dir}/sram_guard.ld"
    "-Wl,--wrap=chip_v7_set_chan_ana")
if(IQ_CAPTURE_PSRAM_QUEUE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE IQ_CAPTURE_PSRAM_QUEUE=1)
endif()
