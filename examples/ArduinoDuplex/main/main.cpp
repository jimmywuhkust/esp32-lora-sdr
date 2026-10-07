#include <Arduino.h>
#include "esp_heap_caps.h"
#include "LoRaRadio.h"
#include "NativePlatform.h"
#include "esp_system.h"
extern "C" bool ring_capture_core1_alive(void);
using namespace lora_sdr;

LoRaRadio radio;
bool ready=false;
Error beginStatus=Error::NotReady;
static void logTransmit(Error status) {
    const auto& tx=radio.lastTransmit();
    Serial.printf("ARDUINO_TX status=%s updates=%u late=%u build_us=%u copy=%u buffer=%08x tone=%08x gain=%u,%u\n",
        errorName(status),tx.updates,tx.lateUpdates,tx.waveformBuildUs,tx.maxCopyCycles,
        unsigned(tx.sourceAddress),unsigned(tx.basebandControl),tx.keyedGain1,tx.keyedGain3);
    Serial.flush();
}
// Three RF banks are reserved before the scheduler starts. Keep the Arduino
// loop within the same 8 KiB stack budget as the native receiver.
size_t getArduinoLoopTaskStackSize(){return 8192;}

void setup() {
#ifdef LORA_SDR_SERIAL_BENCH
    // The optional test fixture owns USB itself. RF packet decoding still
    // uses the same library on the MCU; the host only schedules trials.
    lora_sdr_platform_serial_loop();
#else
    Serial.begin(115200);
    delay(1000);
    Serial.printf("ARDUINO_START cpu=%u core=%u psram=%u heap=%u\n",getCpuFrequencyMhz(),
        unsigned(xPortGetCoreID()),unsigned(ESP.getPsramSize()),unsigned(ESP.getFreeHeap()));
    Serial.flush();
    LoRaSettings settings;
    settings.frequencyMHz=2440.125;
    settings.bandwidthKHz=203.125;
    settings.spreadingFactor=7;
    settings.codingRate=8;
    settings.transmitPowerPercent=75;
    Error status=radio.begin(settings);
    beginStatus=status;
    Serial.printf("ARDUINO_BEGIN status=%s heap=%u largest=%u core1=%u\n",errorName(status),
        unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
        unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),
        unsigned(ring_capture_core1_alive()));
    ready=status==Error::Ok;
#endif
}

void loop() {
    if(!ready){Serial.printf("ARDUINO_NOT_READY status=%s\n",errorName(beginStatus));delay(1000);return;}
#ifdef LORA_SDR_PING_DEMO
    // A bounded autonomous session: USB supplies power/logs, no commands.
    static uint32_t session=esp_random();
    static unsigned sequence=0,accepted=0;
    if(sequence==4){delay(1000);return;}
    delay(sequence?5500:10000);
    uint8_t request[20]={'P','I','N','G'},expected[20];
    memcpy(request+4,&session,4);memcpy(request+8,&sequence,4);
    esp_fill_random(request+12,8);
    memcpy(expected,request,20);memcpy(expected,"PONG",4);
    Serial.printf("PING_SEND seq=%u hex=",sequence);
    for(uint8_t b:request)Serial.printf("%02x",b);
    Serial.println();Serial.flush();
    // Explicit bounded copies cover the peer's finite-window blind time.
    // These are eight RF transmissions of one unique request, not eight
    // successful independent packets. The library itself does not retry.
    Error sent=Error::Ok;
    for(unsigned copy=0;copy<8;copy++) {
        if(copy)delay(350);
        sent=radio.transmit(request,sizeof(request));logTransmit(sent);
        Serial.printf("PING_COPY seq=%u copy=%u status=%s\n",sequence,copy,errorName(sent));
        Serial.flush();if(sent!=Error::Ok)break;
    }
    bool matched=false;uint32_t until=millis()+18000;
    while(sent==Error::Ok && int32_t(until-millis())>0) {
        RxPacket reply;
        if(radio.receive(reply,500)==Error::Ok && reply.length==sizeof(expected) &&
           !memcmp(reply.payload,expected,sizeof(expected))) {
            Serial.printf("PING_RX crc_ok=1 bytes=%u hex=",unsigned(reply.length));
            for(size_t i=0;i<reply.length;i++)Serial.printf("%02x",reply.payload[i]);
            Serial.printf(" soft=%u\n",unsigned(reply.softDecoded));Serial.flush();
            matched=true;break;
        }
        delay(10);
    }
    if(matched)accepted++;
    Serial.printf("PING_RESULT seq=%u tx=%s crc_exact_pong=%u\n",sequence,errorName(sent),unsigned(matched));
    if(++sequence==4)Serial.printf("PING_FINISHED attempts=4 accepted=%u\n",accepted);
    Serial.flush();return;
#endif
    Serial.printf("LISTEN arduino sf=%u window=500\n",radio.configuration().spreadingFactor);
    Serial.flush();
    RxPacket packet;
    Error status=radio.receive(packet,500);
    if(status==Error::Ok) {
        Serial.printf("ARDUINO_RX crc_ok=1 bytes=%u sf=%u cr=%u hex=",
            unsigned(packet.length),packet.spreadingFactor,packet.codingRate);
        for(size_t j=0;j<packet.length;j++)Serial.printf("%02x",packet.payload[j]);
        Serial.println();
        Serial.flush();
        const auto& rx=radio.lastReceive();
        Serial.printf("ARDUINO_CAPTURE samples=%u status=%u drops=%u abandoned=%u soft=%u decode_us=%llu\n",
            rx.captureSamples,rx.captureStatus,rx.captureDrops,rx.captureAbandoned,
            unsigned(packet.softDecoded),static_cast<unsigned long long>(rx.decodeUs));
        Serial.flush();
#ifdef LORA_SDR_PONG_DEMO
        if(packet.length==20 && !memcmp(packet.payload,"PING",4)) {
            uint8_t reply[20];memcpy(reply,packet.payload,20);memcpy(reply,"PONG",4);
            // Wait until the request train ends, then send eight explicit
            // response copies across the initiator's receive/decode windows.
            delay(4500);
            for(unsigned attempt=0;attempt<8;attempt++) {
                if(attempt)delay(350);
                status=radio.transmit(reply,sizeof(reply));
                logTransmit(status);
                Serial.printf("PONG_SEND attempt=%u status=%s hex=",attempt,errorName(status));
                for(uint8_t b:reply)Serial.printf("%02x",b);
                Serial.println();Serial.flush();
            }
        }
#endif
#ifdef LORA_SDR_ECHO_DEMO
        if(packet.length<=251) {
            uint8_t reply[255]={'A','C','K',':'};
            memcpy(reply+4,packet.payload,packet.length);
            delay(20);
            status=radio.transmit(reply,packet.length+4);
            logTransmit(status);
            Serial.printf("ARDUINO_ACK status=%s bytes=%u\n",errorName(status),unsigned(packet.length+4));
            Serial.flush();
        }
#endif
    } else if(status!=Error::ReceiveTimeout) {
        const auto& rx=radio.lastReceive();
        Serial.printf("ARDUINO_ERROR status=%s capture=%u samples=%u drops=%u abandoned=%u\n",
            errorName(status),rx.captureStatus,rx.captureSamples,rx.captureDrops,rx.captureAbandoned);
    }
    delay(10);
}
