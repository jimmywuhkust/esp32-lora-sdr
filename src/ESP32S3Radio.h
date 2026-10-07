#pragma once
#include "LoRaSDR.h"
#include "PacketDecoder.h"

namespace lora_sdr {
struct TxResult {
    PacketInfo packet;
    unsigned updates=0;
    unsigned lateUpdates=0;
    uint32_t sourceAddress=0;
    unsigned maxCopyCycles=0;
    uint32_t waveformBuildUs=0;
    uint32_t playbackClockBefore=0,playbackClockEnabled=0;
    unsigned playbackWindowSamples=0,preflightCopyCycles=0;
    unsigned analogBefore1=0,analogBefore3=0,analogAfter1=0,analogAfter3=0;
    bool analogGainRestored=false;
    uint32_t basebandControl=0, adcControl=0;
    unsigned keyedGain1=0,keyedGain3=0;
};
struct RxResult {
    RxPacket packet;
    RxStatistics decoder;
    unsigned captureStatus=0, captureSamples=0, captureDrops=0, captureAbandoned=0;
    uint64_t captureUs=0, decodeUs=0;
};
class ESP32S3Radio {
public:
    Error begin();
    Error transmit(const uint8_t* payload,size_t length,const Config& config,TxResult& result);
    // Full native finite-window receive is supplied by the IDF component.
    // Arduino builds without that component explicitly return Unsupported.
    Error receive(uint8_t*,size_t,size_t& length);
    Error receive(uint8_t*,size_t,size_t& length,const Config&,RxResult&,uint32_t windowMs=350);
private:
    bool ready_=false;
};
}
