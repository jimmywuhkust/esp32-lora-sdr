#pragma once
#include "ESP32S3Radio.h"
#include <cstring>
#include <cmath>

namespace lora_sdr {
// Small user-facing API; all modulation/packet processing stays in the chip.
// Full RX needs the native IDF component and its supplied sdkconfig.
class LoRaRadio {
public:
    Error begin(double frequencyMHz=2440.125) {
        Error status=setFrequency(frequencyMHz);
        return status==Error::Ok?radio_.begin():status;
    }
    Error setFrequency(double mhz) {
        if(!std::isfinite(mhz)||mhz<2400.2||mhz>2483.3)return Error::InvalidConfig;
        config_.frequencyHz=static_cast<uint32_t>(std::llround(mhz*1e6));return Error::Ok;
    }
    Error setSpreadingFactor(uint8_t sf) {
        if(sf<7||sf>12)return Error::InvalidConfig;
        config_.spreadingFactor=sf;return Error::Ok;
    }
    Error setBandwidth(double khz) {
        if(!std::isfinite(khz))return Error::InvalidConfig;
        if(khz!=203.125&&khz!=406.25&&khz!=812.5)return Error::Unsupported;
        config_.bandwidthHz=static_cast<uint32_t>(std::llround(khz*1000));
        config_.dacWindowSamples=khz==203.125?15000:(khz==406.25?7500:3750);
        return Error::Ok;
    }
    // Pass denominator 5..8, as in other LoRa libraries: 5 means 4/5.
    Error setCodingRate(uint8_t denominator) {
        if(denominator<5||denominator>8)return Error::InvalidConfig;
        config_.codingRate=denominator-4;return Error::Ok;
    }
    Error setPreambleLength(uint16_t symbols) {
        if(symbols<12||symbols>64)return Error::InvalidConfig;
        config_.preambleSymbols=symbols;return Error::Ok;
    }
    Error setSyncWord(uint8_t sync) {config_.syncWord=sync;return Error::Ok;}
    Error setFrequencyCorrection(int32_t hz) {
        if(hz<-50000||hz>50000)return Error::InvalidConfig;
        config_.frequencyCorrectionHz=hz;return Error::Ok;
    }
    // DAC voltage scale, 1..100% of the tested maximum (200 DAC units).
    // The setting is reproducible; antenna power in dBm is not calibrated.
    Error setTransmitPowerPercent(uint8_t percent) {
        if(percent<1||percent>100)return Error::InvalidConfig;
        config_.dacAmplitude=percent*2;return Error::Ok;
    }
    Error send(const uint8_t* payload,size_t length) {
        return radio_.transmit(payload,length,config_,lastTx_);
    }
    Error send(const char* text) {
        return text?send(reinterpret_cast<const uint8_t*>(text),std::strlen(text)):Error::InvalidLength;
    }
    Error receive(RxPacket& packet,uint32_t windowMs=350) {
        uint8_t data[255];size_t length=0;
        Error status=radio_.receive(data,sizeof(data),length,config_,lastRx_,windowMs);
        packet=lastRx_.packet;return status;
    }
    const Config& configuration() const {return config_;}
    const TxResult& lastTransmit() const {return lastTx_;}
    const RxResult& lastReceive() const {return lastRx_;}
private:
    ESP32S3Radio radio_;Config config_;TxResult lastTx_;RxResult lastRx_;
};
}
