#pragma once
#include "LoRaSDR.h"

namespace lora_sdr {
struct RxPacket {
    uint8_t payload[255]{};
    size_t length=0;
    uint16_t crc=0;
    uint8_t spreadingFactor=0, codingRate=0;
    unsigned correctedCodewords=0;
    bool crcPresent=false, crcOk=false;
    bool softDecoded=false;
    float frequencyOffsetHz=0;
    uint64_t sampleIndex=0;
};
// Portable packet processing. No expected payload, Arduino, radio, or PC
// decoder is involved. Symbols use the same convention as Encoder::encode.
class PacketDecoder {
public:
    static bool header(const uint16_t* symbols, const Config&, RxPacket&);
    static size_t symbolCount(const RxPacket&, const Config&);
    static bool decode(const uint16_t* symbols, size_t count, const Config&, RxPacket&);
    // SF7 fallback: per-symbol Gray-bit confidence, positive means bit 1.
    // Header, sync (caller), complete length and payload CRC remain required.
    static bool decodeSoft(const uint16_t*,size_t,const float* grayBitConfidence,const Config&,RxPacket&);
};
struct RxStatistics {
    unsigned candidates=0, headers=0, crcRejected=0, syncRejected=0, packets=0;
};
using PacketCallback=void (*)(const RxPacket&, void*);
// Finite-window, on-device IQ demodulation. Interleaved signed IQ must be
// contiguous. Current RF profile: 250 kSa/s, BW 203125 Hz, SF7..12.
// This processes the window after capture; it is not continuous full duplex.
bool decodeIQ(const int16_t* iq, size_t samples, const Config&, PacketCallback,
              void* context, RxStatistics&, uint64_t firstSample=0, unsigned maxPackets=0);
}
