#include "PacketDecoder.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
using namespace lora_sdr;
static void packet(const RxPacket& p,void*) {
    printf("{\"bytes\":%zu,\"sf\":%u,\"cr\":%u,\"crcOk\":true,\"hex\":\"",p.length,p.spreadingFactor,p.codingRate);
    for(size_t i=0;i<p.length;i++)printf("%02x",p.payload[i]);
    printf("\",\"crc\":\"%04x\",\"sampleIndex\":%llu,\"cfoHz\":%.2f,\"softDecoded\":%s}\n",p.crc,(unsigned long long)p.sampleIndex,p.frequencyOffsetHz,p.softDecoded?"true":"false");
}
int main(int argc,char** argv) {
    Config c;
    if(argc==1){
        unsigned tests=0;
        for(unsigned sf=7;sf<=12;sf++)for(unsigned cr=1;cr<=4;cr++)for(unsigned length: {1u,2u,7u,32u,127u,255u}){
            c.spreadingFactor=sf;c.codingRate=cr;
            uint8_t data[255];for(unsigned i=0;i<length;i++)data[i]=(i*71+length*5+sf*3+cr)&255;
            uint16_t symbols[1100];PacketInfo info;RxPacket p;
            if(Encoder::encode(data,length,c,symbols,1100,info)!=Error::Ok||
               !PacketDecoder::decode(symbols,info.symbolCount,c,p)||p.length!=length||memcmp(data,p.payload,length)){
                printf("FAIL sf=%u cr=%u length=%u\n",sf,cr,length);return 1;
            }tests++;
            // CRC and full window requirements: damaged symbols cannot become
            // an accepted expected-payload substitution, and truncation fails.
            if(PacketDecoder::decode(symbols,info.symbolCount-1,c,p)){puts("FAIL truncation");return 1;}
        }
        unsigned negative=0;
        // Splice a correctly coded payload block from another packet while
        // keeping the original CRC block. FEC must not hide a failed CRC.
        for(unsigned sf=7;sf<=12;sf++)for(unsigned cr=1;cr<=4;cr++){
            c.spreadingFactor=sf;c.codingRate=cr;
            uint8_t a[32],b[32];for(unsigned j=0;j<32;j++)a[j]=b[j]=j*17;
            b[3]^=0x04;uint16_t sa[1100],sb[1100];PacketInfo ia,ib;RxPacket p;
            if(Encoder::encode(a,32,c,sa,1100,ia)!=Error::Ok||Encoder::encode(b,32,c,sb,1100,ib)!=Error::Ok)return 1;
            memcpy(sa+8,sb+8,(4+cr)*sizeof(uint16_t));
            if(PacketDecoder::decode(sa,ia.symbolCount,c,p)){puts("FAIL corrupt CRC accepted");return 1;}
            negative++;
        }
        unsigned soft=0,softNegative=0;
        c.spreadingFactor=7;
        for(unsigned cr=1;cr<=4;cr++)for(unsigned length: {1u,2u,7u,32u,127u,255u}) {
            c.codingRate=cr;uint8_t data[255];
            for(unsigned j=0;j<length;j++)data[j]=(j*71+length*5+cr)&255;
            uint16_t s[1100];PacketInfo info;RxPacket p;
            if(Encoder::encode(data,length,c,s,1100,info)!=Error::Ok)return 1;
            std::vector<float> weights(info.symbolCount*7);
            for(size_t j=8;j<info.symbolCount;j++) {
                unsigned gray=(s[j]-1)&127;gray^=gray>>1;
                for(unsigned bit=0;bit<7;bit++)weights[j*7+bit]=(gray&(1u<<bit))?1.f:-1.f;
            }
            if(!PacketDecoder::decodeSoft(s,info.symbolCount,weights.data(),c,p)||
               !p.softDecoded||p.length!=length||memcmp(data,p.payload,length)) {
                puts("FAIL ideal soft decisions");return 1;
            }
            // Incorrect hard peaks in a body block must not override the
            // independent FFT bit confidence supplied to the soft decoder.
            for(unsigned j=8;j<8+4+cr;j++)s[j]=(s[j]+17)&127;
            if(!PacketDecoder::decodeSoft(s,info.symbolCount,weights.data(),c,p)||memcmp(data,p.payload,length)) {
                puts("FAIL soft recovery");return 1;
            }
            if(PacketDecoder::decodeSoft(s,info.symbolCount-1,weights.data(),c,p)) {
                puts("FAIL soft truncation");return 1;
            }
            soft++;
        }
        for(unsigned cr=1;cr<=4;cr++) {
            c.codingRate=cr;uint8_t a[32],b[32];
            for(unsigned j=0;j<32;j++)a[j]=b[j]=j*17;
            b[3]^=4;uint16_t sa[1100],sb[1100];PacketInfo ia,ib;RxPacket p;
            Encoder::encode(a,32,c,sa,1100,ia);Encoder::encode(b,32,c,sb,1100,ib);
            memcpy(sa+8,sb+8,(4+cr)*sizeof(uint16_t));
            std::vector<float> weights(ia.symbolCount*7);
            for(size_t j=8;j<ia.symbolCount;j++) {
                unsigned gray=(sa[j]-1)&127;gray^=gray>>1;
                for(unsigned bit=0;bit<7;bit++)weights[j*7+bit]=(gray&(1u<<bit))?1.f:-1.f;
            }
            if(PacketDecoder::decodeSoft(sa,ia.symbolCount,weights.data(),c,p)) {
                puts("FAIL soft accepted wrong CRC");return 1;
            }
            softNegative++;
        }
        printf("PASS %u packet codec round trips; truncation rejected; %u CRC negatives rejected; %u soft recovery cases; %u soft CRC negatives\n",tests,negative,soft,softNegative);return 0;
    }
    if(argc!=3)return 2;c.spreadingFactor=atoi(argv[2]);
    FILE* f=fopen(argv[1],"rb");if(!f)return 3;
    fseek(f,0,SEEK_END);size_t bytes=ftell(f);rewind(f);
    std::vector<int16_t> iq(bytes/2);if(fread(iq.data(),1,bytes,f)!=bytes)return 4;fclose(f);
    RxStatistics stats;bool ok=decodeIQ(iq.data(),iq.size()/2,c,packet,nullptr,stats);
    printf("stats ok=%u candidates=%u headers=%u packets=%u crcRejected=%u syncRejected=%u\n",ok,stats.candidates,stats.headers,stats.packets,stats.crcRejected,stats.syncRejected);
    return ok?0:5;
}
