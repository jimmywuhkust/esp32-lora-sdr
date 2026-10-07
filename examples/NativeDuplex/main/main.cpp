#include "LoRaRadio.h"
#include "NativePlatform.h"
#include <cstdio>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
using namespace lora_sdr;

extern "C" void app_main() {
#ifdef LORA_SDR_SERIAL_BENCH
    lora_sdr_platform_serial_loop();
#else
    LoRaRadio radio;
    Error status=radio.begin(2440.125);
    if(status!=Error::Ok){printf("begin: %s\n",errorName(status));return;}
    radio.setSpreadingFactor(7);
    radio.setBandwidth(203.125);
    radio.setCodingRate(8);
    radio.setTransmitPowerPercent(75);
    // Receive by default. To send, explicitly call:
    // status=radio.send("Hello from one ESP32-S3!");
    for(;;){
#ifdef LORA_SDR_ECHO_DEMO
        // Explicit opt-in bench echo. RX is native; no computer commands or
        // expected-payload hints reach the ESP32. No packet is sent at boot.
        printf("LISTEN native sf=7 window=500\n");
#endif
        RxPacket packet;
        status=radio.receive(packet,500);
        if(status==Error::Ok){
            printf("NATIVE_RX crc_ok=1 bytes=%u hex=",unsigned(packet.length));
            for(size_t j=0;j<packet.length;j++)printf("%02x",packet.payload[j]);
            printf("\n");
            fflush(stdout);
#ifdef LORA_SDR_ECHO_DEMO
            if(packet.length<=251){
                // Drain the USB console before TX temporarily masks CPU
                // interrupts. RF decoding never depends on the console.
                vTaskDelay(pdMS_TO_TICKS(20));
                uint8_t reply[255]={'A','C','K',':'};
                memcpy(reply+4,packet.payload,packet.length);
                status=radio.send(reply,packet.length+4);
                printf("NATIVE_ACK status=%s bytes=%u\n",errorName(status),unsigned(packet.length+4));
                fflush(stdout);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
#endif
        }else if(status!=Error::ReceiveTimeout)printf("receive: %s capture=%u samples=%u drops=%u abandoned=%u\n",errorName(status),radio.lastReceive().captureStatus,radio.lastReceive().captureSamples,radio.lastReceive().captureDrops,radio.lastReceive().captureAbandoned);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
#endif
}
