#pragma once
#include "LoRaSDR.h"

namespace lora_sdr {
struct TxResult {
    PacketInfo packet;
    unsigned updates=0;
    unsigned lateUpdates=0;
    uint32_t sourceAddress=0;
    unsigned maxCopyCycles=0;
};
class ESP32S3Radio {
public:
    Error begin();
    Error transmit(const uint8_t* payload,size_t length,const Config& config,TxResult& result);
    // On-device reception is not implemented by this backend. Never report
    // a successful receive operation when only host I/Q capture is available.
    Error receive(uint8_t*,size_t,size_t& length) { length=0;return Error::Unsupported; }
private:
    bool ready_=false;
};
}
