#pragma once
#include <stddef.h>
#include <stdint.h>

namespace lora_sdr {
enum class Transport { Pll, DacWindows, DacStream };
enum class Error { Ok, InvalidConfig, InvalidLength, BufferTooSmall, Unsupported, NoMemory, NotReady, PlaybackTimeout };
struct Config {
    uint32_t frequencyHz = 2440125000u;
    uint32_t bandwidthHz = 203125;
    uint8_t spreadingFactor = 7;
    uint8_t codingRate = 1; // 1..4 => 4/5..4/8
    uint8_t syncWord = 0x12;
    uint16_t preambleSymbols = 16;
    bool explicitHeader = true;
    bool payloadCrc = true;
    bool inverted = true;
    int32_t frequencyCorrectionHz = 15000; // measured for the bench pair, not universal
    uint32_t updateRateHz = 80000;
    uint8_t gainCode = 119; // vendor code; deliberately not labelled dBm
    Transport transport = Transport::Pll;
    uint16_t dacAmplitude = 150; // uncalibrated, signed 10-bit DAC units
    uint16_t dacWindowSamples = 15000;
    uint16_t streamGapSamples = 14; // research: predicted retrigger pause at 40 MS/s
};
struct PacketInfo {
    size_t symbolCount = 0;
    uint16_t crc = 0;
    bool lowDataRateOptimization = false;
    double airtimeMs = 0;
};
class Encoder {
public:
    // No allocation, RF access or Arduino dependency. Output is PHY symbols.
    static Error encode(const uint8_t* payload, size_t length, const Config& config,
                        uint16_t* symbols, size_t capacity, PacketInfo& info);
    static uint16_t crc16(const uint8_t* payload, size_t length);
};
const char* errorName(Error error);
}
