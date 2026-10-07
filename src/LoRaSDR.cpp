#include "LoRaSDR.h"
#include <string.h>

namespace lora_sdr {
uint16_t Encoder::crc16(const uint8_t* payload, size_t length) {
    uint16_t crc=0;
    for(size_t i=0;i+2<length;i++) {
        crc^=static_cast<uint16_t>(payload[i])<<8;
        for(unsigned k=0;k<8;k++)crc=(crc&0x8000)?static_cast<uint16_t>((crc<<1)^0x1021):static_cast<uint16_t>(crc<<1);
    }
    if(length)crc^=payload[length-1];
    if(length>1)crc^=static_cast<uint16_t>(payload[length-2])<<8;
    return crc;
}

static unsigned parity(unsigned x) {
    x^=x>>4;x^=x>>2;x^=x>>1;return x&1;
}
static uint8_t hamming(unsigned nibble,unsigned cr) {
    unsigned a=nibble&1,b=(nibble>>1)&1,c=(nibble>>2)&1,d=(nibble>>3)&1;
    if(cr==1)return (a<<4)|(b<<3)|(c<<2)|(d<<1)|(a^b^c^d);
    return ((a<<7)|(b<<6)|(c<<5)|(d<<4)|((a^b^c)<<3)|((b^c^d)<<2)|((a^b^d)<<1)|(a^c^d))>>(4-cr);
}
Error Encoder::encode(const uint8_t* payload,size_t length,const Config& c,
                      uint16_t* symbols,size_t capacity,PacketInfo& info) {
    info=PacketInfo{};
    if(!payload||length<1||length>255)return Error::InvalidLength;
    if(!symbols||c.spreadingFactor<7||c.spreadingFactor>12||c.codingRate<1||c.codingRate>4||
       c.bandwidthHz<1000||c.bandwidthHz>1625000||c.preambleSymbols<6)return Error::InvalidConfig;
    uint8_t nibbles[519];size_t used=0;
    if(c.explicitHeader) {
        unsigned a=length>>4,b=length&15,d=(c.codingRate<<1)|c.payloadCrc;
        unsigned h4=parity(a);
        unsigned h3=((a>>3)^(b>>3)^(b>>2)^(b>>1)^d)&1;
        unsigned h2=((a>>2)^(b>>3)^b^(d>>3)^(d>>1))&1;
        unsigned h1=((a>>1)^(b>>2)^b^(d>>2)^(d>>1)^d)&1;
        unsigned h0=(a^(b>>1)^(d>>3)^(d>>2)^(d>>1)^d)&1;
        nibbles[used++]=a;nibbles[used++]=b;nibbles[used++]=d;
        nibbles[used++]=h4;nibbles[used++]=(h3<<3)|(h2<<2)|(h1<<1)|h0;
    }
    uint8_t whitening=255;
    for(size_t i=0;i<length;i++) {
        uint8_t value=payload[i]^whitening;
        nibbles[used++]=value&15;nibbles[used++]=value>>4;
        whitening=static_cast<uint8_t>((whitening<<1)|(((whitening>>7)^(whitening>>5)^(whitening>>4)^(whitening>>3))&1));
    }
    info.crc=crc16(payload,length);
    if(c.payloadCrc)for(unsigned shift=0;shift<16;shift+=4)nibbles[used++]=(info.crc>>shift)&15;
    info.lowDataRateOptimization=(static_cast<uint64_t>(1u<<c.spreadingFactor)*1000>static_cast<uint64_t>(c.bandwidthHz)*16);
    size_t position=0,out=0;
    while(position<used) {
        const bool first=position==0;
        unsigned rows=c.spreadingFactor-((first||info.lowDataRateOptimization)?2:0);
        unsigned columns=first?8:4+c.codingRate;
        if(out+columns>capacity){info.symbolCount=0;return Error::BufferTooSmall;}
        uint8_t codewords[12]={};
        for(unsigned r=0;r<rows&&position+r<used;r++)codewords[r]=hamming(nibbles[position+r],first?4:c.codingRate);
        for(unsigned column=0;column<columns;column++) {
            unsigned value=0,p=0;
            for(unsigned j=0;j<rows;j++) {
                unsigned row=(column+rows-1-j)%rows;
                unsigned bit=(codewords[row]>>(columns-1-column))&1;
                value=(value<<1)|bit;p^=bit;
            }
            if(first||info.lowDataRateOptimization)value=(value<<2)|(p<<1);
            unsigned decoded=value;
            for(unsigned shift=1;shift<c.spreadingFactor;shift++)decoded^=value>>shift;
            symbols[out++]=(decoded+1)&((1u<<c.spreadingFactor)-1);
        }
        position+=rows;
    }
    info.symbolCount=out;
    info.airtimeMs=(c.preambleSymbols+4.25+out)*(1u<<c.spreadingFactor)*1000.0/c.bandwidthHz;
    return Error::Ok;
}
const char* errorName(Error error) {
    switch(error) {
    case Error::Ok:return "ok";case Error::InvalidConfig:return "invalid configuration";
    case Error::InvalidLength:return "payload must contain 1..255 bytes";
    case Error::BufferTooSmall:return "symbol buffer too small";case Error::Unsupported:return "unsupported";
    case Error::NoMemory:return "insufficient memory";case Error::NotReady:return "radio not ready";
    case Error::PlaybackTimeout:return "DAC playback timeout";
    case Error::ReceiveTimeout:return "no complete CRC-valid packet in receive window";
    case Error::CaptureGap:return "IQ capture contains a gap";
    }return "unknown error";
}
}
