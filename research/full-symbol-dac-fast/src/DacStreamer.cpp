#include "DacStreamer.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "sdkconfig.h"
#endif
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
#include <Arduino.h>
#include "soc/soc.h"
#include "hal/cpu_hal.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lora_sdr {
// Reader/writer scheduling follows the 0BSD E1 experiment in esp32-sdr-trx:
// write already-read blocks, trigger the next buffer immediately at DONE,
// then fill its last 512 samples before the reader reaches that tail.
// Our samples come from a compact LoRa phase ring rather than a CW oscillator.
struct Cursor {
    const uint8_t *phase;
    const uint32_t *lookup;
    const uint32_t *down;
    const DacSegment *plan;
    unsigned ringLength,segments,segment=0,position=0;
};

static void IRAM_ATTR __attribute__((optimize("O3"))) expand(uint32_t* destination,
                 const uint8_t* phase,const uint32_t* lut,unsigned count) {
    unsigned k=0;
    // Two temporary words keep lookup load/use delays out of the store path.
    // Full unrolling avoids one loop/branch per sample and register spills.
#define PAIR(j) { uint32_t a=lut[phase[k+j]],b=lut[phase[k+j+1]];destination[k+j]=a;destination[k+j+1]=b; }
    for(;k+32<=count;k+=32) {
        PAIR(0) PAIR(2) PAIR(4) PAIR(6) PAIR(8) PAIR(10) PAIR(12) PAIR(14)
        PAIR(16) PAIR(18) PAIR(20) PAIR(22) PAIR(24) PAIR(26) PAIR(28) PAIR(30)
    }
#undef PAIR
    for(;k<count;k++)destination[k]=lut[phase[k]];
    __asm__ volatile("memw" ::: "memory");
}

static void IRAM_ATTR __attribute__((optimize("O3"))) advance(Cursor& c,unsigned samples) {
    while(samples&&c.segment<c.segments) {
        unsigned remaining=c.plan[c.segment].length-c.position;
        unsigned take=samples<remaining?samples:remaining;
        c.position+=take;samples-=take;
        if(c.position==c.plan[c.segment].length){c.segment++;c.position=0;}
    }
}

static void IRAM_ATTR __attribute__((optimize("O3"))) fill(uint32_t* destination,Cursor& c,unsigned samples) {
    unsigned written=0;
    while(written<samples) {
        if(c.segment==c.segments) {
            for(;written<samples;written++)destination[written]=0;
            break;
        }
        const DacSegment& segment=c.plan[c.segment];
        unsigned offset=segment.offset+c.position;
        if(offset>=c.ringLength)offset-=c.ringLength;
        unsigned take=samples-written;
        unsigned remaining=segment.length-c.position;
        if(take>remaining)take=remaining;
        if(take>c.ringLength-offset)take=c.ringLength-offset;
        if(c.lookup)expand(destination+written,c.phase+offset,c.lookup+(segment.down?256:0),take);
        else {
            const uint32_t* source=(segment.down?c.down:reinterpret_cast<const uint32_t*>(c.phase))+offset;
            uint32_t* out=destination+written;
            {
                unsigned k=0;
                for(;k+4<=take;k+=4) {
                    uint32_t a=source[k],b=source[k+1],d=source[k+2],e=source[k+3];
                    out[k]=a;out[k+1]=b;out[k+2]=d;out[k+3]=e;
                }
                for(;k<take;k++)out[k]=source[k];
            }
            __asm__ volatile("memw" ::: "memory");
        }
        advance(c,take);written+=take;
    }
}

void IRAM_ATTR streamDac(const uint8_t* phase,const uint32_t* down,const uint32_t* lookup,unsigned ringLength,
               const DacSegment* plan,unsigned segments,unsigned gapSamples,
               DacStreamStats& stats) {
    stats=DacStreamStats{};
    Cursor cursor;
    cursor.phase=phase;cursor.down=down;cursor.lookup=lookup;cursor.plan=plan;
    cursor.ringLength=ringLength;cursor.segments=segments;
    uint32_t *bank=reinterpret_cast<uint32_t*>(0x3fcd0000);
    constexpr unsigned size=16384,tail=512,block=256,margin=64;
    unsigned total=0;for(unsigned k=0;k<segments;k++)total+=plan[k].length;
    unsigned length=total<size?total:size;
    fill(bank,cursor,length);total-=length;
    unsigned irq=portSET_INTERRUPT_MASK_FROM_ISR();
    REG_WRITE(0x60033d64,length-1);REG_WRITE(0x60033d64,(length-1)|0x80000000u);
    uint32_t started=cpu_hal_get_cycle_count();stats.buffers=1;
    // Nominal airtime cannot bound an overrunning stream. Abort using actual
    // wall cycles as well, below the stock interrupt-watchdog interval.
    const uint32_t stopDeadline=started+55000000u;
    while(total) {
        if(static_cast<int32_t>(cpu_hal_get_cycle_count()-stopDeadline)>=0){stats.timedOut=true;break;}
        unsigned skipped=gapSamples<total?gapSamples:total;
        advance(cursor,skipped);total-=skipped;
        if(!total)break;
        unsigned nextLength=total<size?total:size;
        unsigned head=nextLength<size-tail?nextLength:size-tail;
        for(unsigned w=0;w<head;w+=block) {
            unsigned count=head-w;if(count>block)count=block;
            uint32_t target=started+6u*(w+count-1+margin);
            while(static_cast<int32_t>(cpu_hal_get_cycle_count()-target)<0){}
            if(static_cast<int32_t>(cpu_hal_get_cycle_count()-target)>400)stats.lateBlocks++;
            uint32_t fillStarted=cpu_hal_get_cycle_count();
            fill(bank+w,cursor,count);
            unsigned fillCycles=cpu_hal_get_cycle_count()-fillStarted;
            if(fillCycles>stats.maxFillCycles)stats.maxFillCycles=fillCycles;
        }
        uint32_t timeout=cpu_hal_get_cycle_count()+240000;
        while(!(REG_READ(0x60033d64)&(1u<<18))&&static_cast<int32_t>(cpu_hal_get_cycle_count()-timeout)<0){}
        if(!(REG_READ(0x60033d64)&(1u<<18))){stats.timedOut=true;break;}
        REG_WRITE(0x60033d64,nextLength-1);REG_WRITE(0x60033d64,(nextLength-1)|0x80000000u);
        uint32_t now=cpu_hal_get_cycle_count();
        unsigned elapsed=now-started;
        unsigned gap=elapsed>length*6?elapsed-length*6:0;
        if(gap>stats.maxGapCycles)stats.maxGapCycles=gap;stats.gapCycles+=gap;
        started=now;stats.buffers++;
        fill(bank+head,cursor,nextLength-head);
        total-=nextLength;length=nextLength;
        // Caller caps the entire stream at 250 ms. This isolates copying
        // deadlines from any pending interrupt service during the packet.
    }
    if(!stats.timedOut) {
        uint32_t timeout=cpu_hal_get_cycle_count()+240000;
        while(!(REG_READ(0x60033d64)&(1u<<18))&&static_cast<int32_t>(cpu_hal_get_cycle_count()-timeout)<0){}
        if(!(REG_READ(0x60033d64)&(1u<<18)))stats.timedOut=true;
    }
    REG_WRITE(0x60033d64,0);portCLEAR_INTERRUPT_MASK_FROM_ISR(irq);
}
}
#else
namespace lora_sdr {
void streamDac(const uint8_t*,const uint32_t*,const uint32_t*,unsigned,const DacSegment*,unsigned,unsigned,DacStreamStats& stats){stats.timedOut=true;}
}
#endif
