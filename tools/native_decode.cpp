#include "PacketDecoder.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
using namespace lora_sdr;
static void packet(const RxPacket& p,void*) {
    printf("{\"bytes\":%zu,\"sf\":%u,\"cr\":%u,\"crcOk\":true,\"hex\":\"",p.length,p.spreadingFactor,p.codingRate);
    for(size_t i=0;i<p.length;i++)printf("%02x",p.payload[i]);
    printf("\",\"crc\":\"%04x\",\"sampleIndex\":%llu,\"cfoHz\":%.2f}\n",p.crc,(unsigned long long)p.sampleIndex,p.frequencyOffsetHz);
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
        printf("PASS %u packet codec round trips; truncation rejected; %u CRC negatives rejected\n",tests,negative);return 0;
    }
    if(argc!=3)return 2;c.spreadingFactor=atoi(argv[2]);
    FILE* f=fopen(argv[1],"rb");if(!f)return 3;
    fseek(f,0,SEEK_END);size_t bytes=ftell(f);rewind(f);
    std::vector<int16_t> iq(bytes/2);if(fread(iq.data(),1,bytes,f)!=bytes)return 4;fclose(f);
    RxStatistics stats;bool ok=decodeIQ(iq.data(),iq.size()/2,c,packet,nullptr,stats);
    printf("stats ok=%u candidates=%u headers=%u packets=%u crcRejected=%u syncRejected=%u\n",ok,stats.candidates,stats.headers,stats.packets,stats.crcRejected,stats.syncRejected);
    return ok?0:5;
}
