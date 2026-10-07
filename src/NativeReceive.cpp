#include "ESP32S3Radio.h"
#include <cstring>
#include <cstdlib>
#if defined(ESP_PLATFORM) && (!defined(ARDUINO_ARCH_ESP32) || defined(LORA_SDR_NATIVE_BACKEND))
#include "NativePlatform.h"
#include "esp_timer.h"
#endif
namespace lora_sdr {
Error ESP32S3Radio::receive(uint8_t* data,size_t capacity,size_t& length) {
    Config c;RxResult result;return receive(data,capacity,length,c,result,350);
}
Error ESP32S3Radio::receive(uint8_t* data,size_t capacity,size_t& length,const Config& c,RxResult& result,uint32_t ms) {
    length=0;result=RxResult{};
#if defined(ESP_PLATFORM) && (!defined(ARDUINO_ARCH_ESP32) || defined(LORA_SDR_NATIVE_BACKEND))
    if(!ready_)return Error::NotReady;
    if(!data||!capacity||ms<50||ms>900||c.frequencyHz<2400200000u||c.frequencyHz>2483300000u)return Error::InvalidConfig;
    if(c.bandwidthHz!=203125||c.spreadingFactor<7||c.spreadingFactor>12||!c.explicitHeader||!c.payloadCrc)return Error::Unsupported;
    lora_native_capture_t capture{};int status=lora_sdr_platform_capture(c.frequencyHz,ms,&capture);
    result.captureStatus=capture.capture_status;result.captureSamples=capture.samples;
    result.captureDrops=capture.drops;result.captureAbandoned=capture.abandoned;result.captureUs=capture.capture_us;
    if(status)return status==2?Error::NoMemory:Error::CaptureGap;
    int64_t start=esp_timer_get_time();
    auto store=[](const RxPacket& p,void* ctx){static_cast<RxResult*>(ctx)->packet=p;};
    bool ok=decodeIQ(capture.iq,capture.samples,c,store,&result,result.decoder,capture.first_sample,1);
    result.decodeUs=esp_timer_get_time()-start;free(capture.iq);
    if(!ok)return Error::NoMemory;
    if(!result.packet.crcOk)return Error::ReceiveTimeout;
    if(capacity<result.packet.length)return Error::BufferTooSmall;
    length=result.packet.length;memcpy(data,result.packet.payload,length);return Error::Ok;
#else
    (void)data;(void)capacity;(void)c;(void)ms;return Error::Unsupported;
#endif
}
}
