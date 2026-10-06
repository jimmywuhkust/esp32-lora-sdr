#include <Arduino.h>
#include <LoRaSDR.h>
#include <ESP32S3Radio.h>
#include "DacStreamer.h"
#include <ctype.h>
using namespace lora_sdr;
ESP32S3Radio radio;
Config config;
void setup() {
    Serial.setRxBufferSize(8192);Serial.begin(115200);delay(1000);
    Serial.println("LoRaSDR native bench: no automatic RF transmission");
    Serial.println(errorName(radio.begin()));
}
void loop() {
    if(!Serial.available()){delay(1);return;}
    String line=Serial.readStringUntil('\n');line.trim();
    if(line=="INFO"){Serial.println("LoRaSDR native S3 0.1");return;}
    if(line=="LUTTEST") {
        unsigned cycles;bool ok=dacLutSelfTest(cycles);
        Serial.printf("LUTTEST passed=%u max=%u budget=1536\n",ok,cycles);Serial.flush();return;
    }
    if(line=="BANKIDLE"||line=="BANKPLAY") {
        unsigned bad,first,cycles;bool ok=dacBankSelfTest(line=="BANKPLAY",bad,first,cycles);
        Serial.printf("BANK ok=%u bad=%u first=%u c=%u\n",ok,bad,first,cycles);
        Serial.flush();return;
    }
    if(line=="DAC"){config.transport=Transport::DacWindows;Serial.println("DAC selected");return;}
    if(line=="STREAM"){config.transport=Transport::DacStream;Serial.println("STREAM selected");return;}
    if(line.startsWith("GAP ")) {
        unsigned value;char extra;
        if(sscanf(line.c_str(),"GAP %u %c",&value,&extra)!=1||value>256){Serial.println("ERR gap");return;}
        config.streamGapSamples=value;Serial.printf("GAP %u\n",value);return;
    }
    if(line=="PLL"){config.transport=Transport::Pll;Serial.println("PLL selected");return;}
    if(line.startsWith("AMP ")||line.startsWith("WIN ")||line.startsWith("PRE ")||line.startsWith("FREQ ")||line.startsWith("CFO ")) {
        char key[5],extra;long value;
        if(sscanf(line.c_str(),"%4s %ld %c",key,&value,&extra)!=2){Serial.println("ERR setting");return;}
        if(!strcmp(key,"AMP")&&value>=1&&value<=200)config.dacAmplitude=value;
        else if(!strcmp(key,"WIN")&&value>=1000&&value<=16380)config.dacWindowSamples=value;
        else if(!strcmp(key,"PRE")&&value>=12&&value<=64)config.preambleSymbols=value;
        else if(!strcmp(key,"FREQ")&&value>=2400200&&value<=2483300)config.frequencyHz=static_cast<uint32_t>(value)*1000u;
        else if(!strcmp(key,"CFO")&&value>=-50000&&value<=50000)config.frequencyCorrectionHz=value;
        else {Serial.println("ERR setting");return;}
        Serial.printf("%s %ld\n",key,value);return;
    }
    if(line.startsWith("GAIN ")) {
        unsigned gain;char extra;
        if(sscanf(line.c_str(),"GAIN %u %c",&gain,&extra)!=1||gain<64||gain>200){Serial.println("ERR gain");return;}
        config.gainCode=gain;Serial.printf("GAIN %u raw vendor code, not dBm\n",gain);return;
    }
    bool tx=line.startsWith("TX "),encode=line.startsWith("ENC ");
    if(!tx&&!encode){Serial.println("ERR command");return;}
    unsigned sf,cr;char hex[511],extra;
    if(sscanf(line.c_str(),tx?"TX %u %u %510s %c":"ENC %u %u %510s %c",&sf,&cr,hex,&extra)!=3||
       sf<7||sf>12||cr<1||cr>4){Serial.println("ERR arguments");return;}
    size_t length=strlen(hex)/2;if(strlen(hex)%2||length<1||length>255){Serial.println("ERR length");return;}
    uint8_t payload[255];
    for(size_t j=0;j<length*2;j++)if(!isxdigit(static_cast<unsigned char>(hex[j]))){Serial.println("ERR hex");return;}
    for(size_t j=0;j<length;j++) {
        unsigned byte;if(sscanf(hex+2*j,"%2x",&byte)!=1){Serial.println("ERR hex");return;}payload[j]=byte;
    }
    config.spreadingFactor=sf;config.codingRate=cr;
    if(encode) {
        uint16_t symbols[1024];PacketInfo info;
        Error result=Encoder::encode(payload,length,config,symbols,1024,info);
        if(result!=Error::Ok){Serial.println(errorName(result));return;}
        Serial.printf("SYMBOLS %u %04x",static_cast<unsigned>(info.symbolCount),info.crc);
        for(size_t j=0;j<info.symbolCount;j++)Serial.printf(" %u",symbols[j]);Serial.println();
    } else {
        TxResult result;Serial.println("TXSTART NATIVE");Serial.flush();
        Error error=radio.transmit(payload,length,config,result);
        delay(2);
        Serial.printf("TXEND NATIVE %s %u %u %.3f c=%u g=%u\n",errorName(error),result.updates,result.lateUpdates,result.packet.airtimeMs,result.maxCopyCycles,result.maxGapCycles);
        Serial.flush();
    }
}
