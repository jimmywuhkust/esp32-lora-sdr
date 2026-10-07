#include "PacketDecoder.h"
#include <string.h>
#include <algorithm>
#ifdef LORA_SDR_DIAGNOSTIC_TRACE
#include <cstdio>
#endif
namespace lora_sdr {
static unsigned parity(unsigned v) {v^=v>>4;v^=v>>2;v^=v>>1;return v&1;}
static uint8_t code(unsigned x,unsigned cr) {
    unsigned a=x&1,b=(x>>1)&1,c=(x>>2)&1,d=(x>>3)&1;
    if(cr==1)return (a<<4)|(b<<3)|(c<<2)|(d<<1)|(a^b^c^d);
    return ((a<<7)|(b<<6)|(c<<5)|(d<<4)|((a^b^c)<<3)|((b^c^d)<<2)|((a^b^d)<<1)|(a^c^d))>>(4-cr);
}
static unsigned distance(unsigned a,unsigned b) {unsigned v=a^b,n=0;while(v){n+=v&1;v>>=1;}return n;}
static uint8_t fec(unsigned word,unsigned cr,unsigned& corrected) {
    unsigned best=9,choice=0,ties=0;
    for(unsigned n=0;n<16;n++) {unsigned d=distance(word,code(n,cr));if(d<best){best=d;choice=n;ties=1;}else if(d==best)ties++;}
    if(ties==1&&best<=1) {if(best)corrected++;return choice;}
    // Ambiguous words retain their systematic data, never guess a payload.
    unsigned v=word>>cr;
    return ((v>>3)&1)|((v>>1)&2)|((v<<1)&4)|((v<<3)&8);
}
static void block(const uint16_t* s,unsigned rows,unsigned columns,bool reduced,uint8_t* n,unsigned& corrected) {
    unsigned words[12]={};
    for(unsigned col=0;col<columns;col++) {
        unsigned v=reduced?s[col]/4:((s[col]-1)&((1u<<rows)-1));v^=v>>1;
        for(unsigned j=0;j<rows;j++) {
            unsigned r=(col+rows-1-j)%rows;
            words[r]|=((v>>(rows-1-j))&1)<<(columns-1-col);
        }
    }
    for(unsigned r=0;r<rows;r++)n[r]=fec(words[r],columns-4,corrected);
}
bool PacketDecoder::header(const uint16_t* s,const Config& c,RxPacket& p) {
    if(!s||c.spreadingFactor<7||c.spreadingFactor>12)return false;
    p=RxPacket{};p.spreadingFactor=c.spreadingFactor;
    if(!c.explicitHeader)return false; // implicit RX requires a separate length contract
    uint8_t n[12];block(s,c.spreadingFactor-2,8,true,n,p.correctedCodewords);
    unsigned a=n[0],b=n[1],d=n[2];
    unsigned h4=parity(a),h3=((a>>3)^(b>>3)^(b>>2)^(b>>1)^d)&1;
    unsigned h2=((a>>2)^(b>>3)^b^(d>>3)^(d>>1))&1;
    unsigned h1=((a>>1)^(b>>2)^b^(d>>2)^(d>>1)^d)&1;
    unsigned h0=(a^(b>>1)^(d>>3)^(d>>2)^(d>>1)^d)&1;
    p.length=16*a+b;p.codingRate=d>>1;p.crcPresent=d&1;
    return p.length>0&&p.length<=255&&p.codingRate>=1&&p.codingRate<=4&&
           (n[3]&1)==h4&&n[4]==((h3<<3)|(h2<<2)|(h1<<1)|h0);
}
size_t PacketDecoder::symbolCount(const RxPacket& p,const Config& c) {
    bool ldro=(static_cast<uint64_t>(1u<<c.spreadingFactor)*1000>static_cast<uint64_t>(c.bandwidthHz)*16);
    size_t used=5+2*p.length+(p.crcPresent?4:0),first=c.spreadingFactor-2;
    unsigned rows=c.spreadingFactor-(ldro?2:0);
    return 8+(used>first?(used-first+rows-1)/rows*(4+p.codingRate):0);
}
static bool finish(const uint8_t* n,unsigned corrected,RxPacket& p) {
    uint8_t w=255;
    for(size_t i=0;i<p.length;i++){
        p.payload[i]=(n[5+2*i]|(n[6+2*i]<<4))^w;
        w=static_cast<uint8_t>((w<<1)|(((w>>7)^(w>>5)^(w>>4)^(w>>3))&1));
    }
    unsigned k=5+2*p.length;
    p.crc=n[k]|(n[k+1]<<4)|(n[k+2]<<8)|(n[k+3]<<12);
    p.correctedCodewords=corrected;p.crcOk=p.crc==Encoder::crc16(p.payload,p.length);
    return p.crcOk;
}
bool PacketDecoder::decode(const uint16_t* s,size_t count,const Config& c,RxPacket& p) {
    if(count<8||!header(s,c,p)||!p.crcPresent||count<symbolCount(p,c))return false;
    uint8_t n[540]={};unsigned corrected=0;
    unsigned first=c.spreadingFactor-2;block(s,first,8,true,n,corrected);
    bool ldro=(static_cast<uint64_t>(1u<<c.spreadingFactor)*1000>static_cast<uint64_t>(c.bandwidthHz)*16);
    unsigned rows=c.spreadingFactor-(ldro?2:0),cols=4+p.codingRate,at=first;
    for(size_t i=8;i+cols<=symbolCount(p,c);i+=cols){block(s+i,rows,cols,ldro,n+at,corrected);at+=rows;}
    return finish(n,corrected,p);
}
bool PacketDecoder::decodeSoft(const uint16_t* s,size_t count,const float* confidence,const Config& c,RxPacket& p) {
    if(!confidence||c.spreadingFactor!=7||count<8||!header(s,c,p)||!p.crcPresent||count<symbolCount(p,c))return false;
    uint8_t n[540]={};unsigned corrected=0;
    block(s,5,8,true,n,corrected);unsigned columns=4+p.codingRate,at=5;
    struct Alternative {unsigned index;uint8_t value;float gap;bool oldChanged,newChanged;};
    Alternative alternatives[6];unsigned alternativeCount=0;
    for(size_t i=8;i+columns<=symbolCount(p,c);i+=columns) {
        float weights[7][8]={};unsigned hard[7]={};
        for(unsigned col=0;col<columns;col++) {
            unsigned gray=(s[i+col]-1)&127;gray^=gray>>1;
            for(unsigned j=0;j<7;j++) {
                unsigned row=(col+6-j)%7;
                weights[row][columns-1-col]=confidence[(i+col)*7+6-j];
                hard[row]|=((gray>>(6-j))&1)<<(columns-1-col);
            }
        }
        for(unsigned row=0;row<7;row++) {
            float best=-1000,second=-1000;unsigned choice=0,runner=0;
            for(unsigned value=0;value<16;value++) {
                unsigned encoded=code(value,p.codingRate);float score=0;
                for(unsigned bit=0;bit<columns;bit++)score+=(encoded&(1u<<bit))?weights[row][bit]:-weights[row][bit];
                if(score>best){second=best;runner=choice;best=score;choice=value;}
                else if(score>second){second=score;runner=value;}
            }
            n[at+row]=choice;
#ifdef LORA_SDR_DIAGNOSTIC_TRACE
            printf("SOFT row=%u at=%u cr=%u hard=%u choice=%u encoded=%u score=%f\n",row,at,p.codingRate,hard[row],choice,code(choice,p.codingRate),best);
#endif
            if(code(choice,p.codingRate)!=hard[row])corrected++;
            // Bounded Chase list for genuinely ambiguous payload words.
            // Keep the received CRC and the last two payload bytes fixed:
            // LoRa XORs those bytes directly into its CRC. Never solve for a
            // checksum or use expected application bytes as a decoding hint.
            unsigned index=at+row,end=5+2*(p.length>2?p.length-2:0);
            if(index>=5&&index<end&&best-second<.75f) {
                Alternative a={index,uint8_t(runner),best-second,
                    code(choice,p.codingRate)!=hard[row],code(runner,p.codingRate)!=hard[row]};
                unsigned insert=0;
                while(insert<alternativeCount&&alternatives[insert].gap<=a.gap)insert++;
                if(insert<6) {
                    unsigned last=std::min(5u,alternativeCount);
                    for(unsigned k=last;k>insert;k--)alternatives[k]=alternatives[k-1];
                    alternatives[insert]=a;if(alternativeCount<6)alternativeCount++;
                }
            }
        }
        at+=7;
    }
    if(finish(n,corrected,p)){p.softDecoded=true;return true;}
    if(!alternativeCount)return false;
    uint8_t original[6];for(unsigned k=0;k<alternativeCount;k++)original[k]=n[alternatives[k].index];
    RxPacket accepted;unsigned valid=0;
    // At most 63 alternatives; accept only one complete CRC-valid payload.
    for(unsigned mask=1;mask<(1u<<alternativeCount);mask++) {
        unsigned changed=corrected;
        for(unsigned k=0;k<alternativeCount;k++) {
            const auto& a=alternatives[k];bool replace=mask&(1u<<k);
            n[a.index]=replace?a.value:original[k];
            if(replace){changed-=a.oldChanged;changed+=a.newChanged;}
        }
        RxPacket candidate=p;
        if(finish(n,changed,candidate)) {
            if(++valid>1)return false;
            accepted=candidate;
        }
    }
    if(valid==1){p=accepted;p.softDecoded=true;p.crcAided=true;return true;}
    return false;
}
}
