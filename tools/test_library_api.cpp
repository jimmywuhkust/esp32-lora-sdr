#include "LoRaRadio.h"
#include <cassert>
#include <limits>
#include <vector>
#include <cstdio>

// Exercise application-facing configuration/dispatch without claiming RF.
using namespace lora_sdr;
static unsigned begins=0, transmissions=0;
static Config dispatched;
static std::vector<uint8_t> bytes;
static Error backendStatus=Error::Ok;
namespace lora_sdr {
Error ESP32S3Radio::begin(){++begins;return backendStatus;}
Error ESP32S3Radio::transmit(const uint8_t* data,size_t length,const Config& c,TxResult& r){
    ++transmissions;dispatched=c;r=TxResult{};
    if(!data||!length||length>255)return Error::InvalidLength;
    bytes.assign(data,data+length);return backendStatus;
}
Error ESP32S3Radio::receive(uint8_t*,size_t,size_t& length,const Config&,RxResult& r,uint32_t){
    length=0;r=RxResult{};return Error::Unsupported;
}
}

int main(){
    LoRaRadio radio;
    LoRaSettings settings;
    settings.frequencyMHz=2476.125;settings.bandwidthKHz=406.25;
    settings.spreadingFactor=8;settings.codingRate=7;settings.syncWord=0x34;
    settings.preambleLength=32;settings.transmitPowerPercent=25;
    settings.frequencyCorrectionHz=-12000;
    assert(radio.begin(settings)==Error::Ok&&begins==1&&transmissions==0);
    const uint8_t binary[]={0,0xff,0x80,0x12};
    assert(radio.transmit(binary,sizeof(binary))==Error::Ok);
    assert(bytes==std::vector<uint8_t>(binary,binary+sizeof(binary)));
    assert(dispatched.frequencyHz==2476125000u&&dispatched.bandwidthHz==406250);
    assert(dispatched.spreadingFactor==8&&dispatched.codingRate==3);
    assert(dispatched.syncWord==0x34&&dispatched.preambleSymbols==32);
    assert(dispatched.dacAmplitude==50&&dispatched.dacWindowSamples==7500);
    assert(dispatched.frequencyCorrectionHz==-12000);

    // A late invalid field must not apply earlier valid fields or initialize RF.
    LoRaSettings invalid=settings;invalid.frequencyMHz=2403.125;
    invalid.transmitPowerPercent=0;
    assert(radio.begin(invalid)==Error::InvalidConfig&&begins==1);
    assert(radio.configuration().frequencyHz==2476125000u);
    assert(radio.configuration().dacAmplitude==50);
    invalid=settings;invalid.codingRate=9;
    assert(radio.configure(invalid)==Error::InvalidConfig);
    invalid=settings;invalid.bandwidthKHz=125;
    assert(radio.configure(invalid)==Error::Unsupported);
    invalid=settings;invalid.frequencyMHz=std::numeric_limits<double>::quiet_NaN();
    assert(radio.configure(invalid)==Error::InvalidConfig);
    assert(radio.configuration().codingRate==3&&transmissions==1);

    assert(radio.setBandwidth(812.5)==Error::Ok);
    assert(radio.configuration().dacWindowSamples==3750);
    assert(radio.setBandwidth(203.125)==Error::Ok);
    assert(radio.configuration().dacWindowSamples==15000);
    assert(radio.setSpreadingFactor(12)==Error::Ok); // RX setting; not TX proof.
    assert(radio.setSpreadingFactor(13)==Error::InvalidConfig);
    assert(radio.configuration().spreadingFactor==12);

    backendStatus=Error::PlaybackTimeout;
    assert(radio.transmit("test")==Error::PlaybackTimeout);
    assert(bytes==std::vector<uint8_t>({'t','e','s','t'}));
    assert(radio.send(binary,sizeof(binary))==Error::PlaybackTimeout);
    assert(bytes==std::vector<uint8_t>(binary,binary+sizeof(binary)));
    unsigned calls=transmissions;
    assert(radio.transmit(static_cast<const char*>(nullptr))==Error::InvalidLength);
    assert(transmissions==calls);
    backendStatus=Error::NotReady;
    assert(radio.begin(settings)==Error::NotReady);
    RxPacket packet;packet.crcOk=true;packet.length=123;
    assert(radio.receive(packet)==Error::Unsupported);
    assert(!packet.crcOk&&packet.length==0);
    puts("Library API: atomic profiles, binary dispatch, errors and unsupported RX passed (no RF)");
}
