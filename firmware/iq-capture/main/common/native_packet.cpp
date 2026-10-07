#include "native_packet.h"
#include "PacketDecoder.h"
#include "ESP32S3Radio.h"
#include "LoRaRadio.h"
#include "NativePlatform.h"
extern "C" {
#include "burst_serial.h"
}
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include <cstdio>
#include <cstring>
#include <inttypes.h>
using namespace lora_sdr;
static LoRaRadio nativeRadio;
static bool serialReadyPending;
extern "C" void native_capture_ready(void) {
    if(serialReadyPending){
        serialReadyPending=false;
        const char* line="RXPACK READY\n";burst_serial_send(line,strlen(line));
    }
}
extern "C" bool native_setting_command(const char* command) {
    if(strncmp(command,"LSET ",5))return false;
    char key[12],extra;int value;Error status=Error::InvalidConfig;
    if(sscanf(command,"LSET %11s %d %c",key,&value,&extra)!=2){
        const char* err="ERR lset_args\n";burst_serial_send(err,strlen(err));return true;
    }
    if(!strcmp(key,"POWER")&&value>=1&&value<=100)status=nativeRadio.setTransmitPowerPercent(value);
    else if(!strcmp(key,"FREQ")&&value>=2400200&&value<=2483300)status=nativeRadio.setFrequency(value/1000.0);
    else if(!strcmp(key,"BW")&&(value==203125||value==406250||value==812500))status=nativeRadio.setBandwidth(value/1000.0);
    else if(!strcmp(key,"PRE")&&value>=12&&value<=64)status=nativeRadio.setPreambleLength(value);
    else if(!strcmp(key,"SYNC")&&value>=0&&value<=255)status=nativeRadio.setSyncWord(value);
    else if(!strcmp(key,"CFO"))status=nativeRadio.setFrequencyCorrection(value);
    char line[100];int n=snprintf(line,sizeof(line),"LSET %s %d status=%s\n",key,value,errorName(status));
    burst_serial_send(line,n);return true;
}
extern "C" bool native_transmit_hex(unsigned sf,unsigned cr,const char* hex) {
    unsigned length=strlen(hex)/2;
    if(strlen(hex)%2||!length||length>255||sf<7||sf>12||cr<1||cr>4)return false;
    uint8_t data[255];
    for(unsigned j=0;j<2*length;j++)if(!((hex[j]>='0'&&hex[j]<='9')||(hex[j]>='a'&&hex[j]<='f')||(hex[j]>='A'&&hex[j]<='F')))return false;
    for(unsigned j=0;j<length;j++){unsigned v;sscanf(hex+2*j,"%2x",&v);data[j]=v;}
    nativeRadio.setSpreadingFactor(sf);nativeRadio.setCodingRate(cr+4);
    Error status=nativeRadio.begin(nativeRadio.configuration().frequencyHz/1e6);
    if(status==Error::Ok)status=nativeRadio.send(data,length);
    const TxResult& result=nativeRadio.lastTransmit();
    char line[300];int n=snprintf(line,sizeof(line),"TXEND NATIVE %s %u %u %.3f buffer=%08x copy=%u tone=%08x adc=%08x gain=%u,%u build_us=%u window=%u preflight=%u\n",errorName(status),result.updates,result.lateUpdates,result.packet.airtimeMs,(unsigned)result.sourceAddress,result.maxCopyCycles,(unsigned)result.basebandControl,(unsigned)result.adcControl,result.keyedGain1,result.keyedGain3,(unsigned)result.waveformBuildUs,result.playbackWindowSamples,result.preflightCopyCycles);
    burst_serial_send(line,n);return true;
}
static void emit(const RxPacket& p,void*) {
    char hex[511];for(unsigned i=0;i<p.length;i++)sprintf(hex+2*i,"%02x",p.payload[i]);
    char line[800];int n=snprintf(line,sizeof(line),"RXPACKET {\"hex\":\"%s\",\"bytes\":%u,\"sf\":%u,\"codingRate\":%u,\"crcOk\":true,\"crcHex\":\"%04x\",\"cfoHz\":%.2f,\"sampleIndex\":%" PRIu64 ",\"correctedCodewords\":%u,\"softDecoded\":%s,\"crcAided\":%s,\"source\":\"ESP32 native decoder\"}\n",
        hex,(unsigned)p.length,p.spreadingFactor,p.codingRate,p.crc,p.frequencyOffsetHz,p.sampleIndex,p.correctedCodewords,p.softDecoded?"true":"false",p.crcAided?"true":"false");
    burst_serial_send(line,n);
}
extern "C" bool native_decode_iq(const int16_t* iq,unsigned samples,unsigned sf,unsigned sync,uint64_t first) {
    Config c;c.spreadingFactor=sf;c.syncWord=sync;RxStatistics stats;
    int64_t start=esp_timer_get_time();
    bool ok=decodeIQ(iq,samples,c,emit,nullptr,stats,first,1);
    char line[220];int n=snprintf(line,sizeof(line),"RXDECODE {\"ok\":%s,\"samples\":%u,\"candidates\":%u,\"headers\":%u,\"packets\":%u,\"crcRejected\":%u,\"syncRejected\":%u,\"decodeUs\":%" PRIi64 "}\n",ok?"true":"false",samples,stats.candidates,stats.headers,stats.packets,stats.crcRejected,stats.syncRejected,esp_timer_get_time()-start);
    burst_serial_send(line,n);return ok;
}
struct __attribute__((packed)) Header {
    uint32_t magic,seq;uint64_t index;uint16_t samples;uint8_t bits,flags;uint16_t dec;uint8_t gain,shift;
};
extern "C" bool native_frames_to_iq(const uint8_t* raw,unsigned bytes,int16_t** output,unsigned* outputSamples,uint64_t* outputFirst) {
    *output=nullptr;*outputSamples=0;*outputFirst=0;
    // Reject transport/capture discontinuities; never bridge them with zeros.
    unsigned pos=0,samples=0,frames=0;uint64_t first=0,next=0;uint32_t seq=0;
    while(pos<bytes){
        if(bytes-pos<28)return false;
        Header h;memcpy(&h,raw+pos,24);
        unsigned size=28+h.samples*h.bits/4;uint32_t crc;
        if(h.magic!=0x31535149||(h.bits!=4&&h.bits!=8&&h.bits!=16)||h.dec!=64||!h.samples||h.samples>1024||h.flags||size>bytes-pos)return false;
        memcpy(&crc,raw+pos+size-4,4);if(esp_rom_crc32_le(0,raw+pos,size-4)!=crc)return false;
        if(frames&&(h.index!=next||h.seq!=seq+1))return false;
        if(!frames)first=h.index;
        next=h.index+h.samples;seq=h.seq;samples+=h.samples;pos+=size;frames++;
    }
    if(!samples||samples>250000)return false;
    int16_t* iq=static_cast<int16_t*>(heap_caps_malloc(samples*4,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!iq)return false;
    pos=0;unsigned at=0;
    while(pos<bytes){
        Header h;memcpy(&h,raw+pos,24);const uint8_t* p=raw+pos+24;
        if(h.bits==16)memcpy(iq+2*at,p,h.samples*4);
        else for(unsigned j=0;j<h.samples;j++) {
            if(h.bits==4){iq[2*(at+j)]=int(p[j]&15)-((p[j]&8)?16:0);iq[2*(at+j)+1]=int(p[j]>>4)-((p[j]&128)?16:0);}
            else {iq[2*(at+j)]=static_cast<int8_t>(p[2*j]);iq[2*(at+j)+1]=static_cast<int8_t>(p[2*j+1]);}
        }
        at+=h.samples;pos+=28+h.samples*h.bits/4;
    }
    *output=iq;*outputSamples=samples;*outputFirst=first;return true;
}
extern "C" bool native_decode_frames(const uint8_t* raw,unsigned bytes,unsigned sf,unsigned sync) {
    int16_t* iq;unsigned samples;uint64_t first;
    if(!native_frames_to_iq(raw,bytes,&iq,&samples,&first))return false;
    bool ok=native_decode_iq(iq,samples,sf,sync,first);free(iq);return ok;
}
extern "C" void native_receive_command(unsigned sf,unsigned ms) {
    nativeRadio.setSpreadingFactor(sf);RxPacket packet;
    Error status=nativeRadio.begin(nativeRadio.configuration().frequencyHz/1e6);
    serialReadyPending=status==Error::Ok;
    if(status==Error::Ok)status=nativeRadio.receive(packet,ms);
    serialReadyPending=false;
    const RxResult& result=nativeRadio.lastReceive();
    if(status==Error::Ok)emit(packet,nullptr);
    char line[300];int n=snprintf(line,sizeof(line),"RXDECODE {\"ok\":%s,\"samples\":%u,\"captureStatus\":%u,\"drops\":%u,\"abandoned\":%u,\"candidates\":%u,\"headers\":%u,\"packets\":%u,\"decodeUs\":%" PRIu64 ",\"error\":\"%s\"}\n",status==Error::Ok?"true":"false",result.captureSamples,result.captureStatus,result.captureDrops,result.captureAbandoned,result.decoder.candidates,result.decoder.headers,result.decoder.packets,result.decodeUs,errorName(status));
    burst_serial_send(line,n);
    const char* end=status==Error::Ok?"RXPACKEND OK\n":"RXPACKEND FAIL\n";burst_serial_send(end,strlen(end));
}
// Optional finite raw recording for RF diagnosis. Applications still call
// receive() and decode on the ESP32; this fixture never feeds expected bytes.
extern "C" void native_capture_iq_debug(unsigned ms) {
    Error begin=nativeRadio.begin(nativeRadio.configuration().frequencyHz/1e6);
    lora_native_capture_t capture{};
    serialReadyPending=begin==Error::Ok;
    int status=begin==Error::Ok?lora_sdr_platform_capture(nativeRadio.configuration().frequencyHz,ms,&capture):4;
    serialReadyPending=false;
    if(status){const char* err="RXIQ FAIL\n";burst_serial_send(err,strlen(err));return;}
    uint32_t crc=esp_rom_crc32_le(0,reinterpret_cast<uint8_t*>(capture.iq),capture.samples*4);
    char header[160];int n=snprintf(header,sizeof(header),"RXIQ %u %08x status=%u drops=%u abandoned=%u\n",
        capture.samples,(unsigned)crc,capture.capture_status,capture.drops,capture.abandoned);
    burst_serial_send(header,n);burst_serial_send(capture.iq,capture.samples*4);free(capture.iq);
}
