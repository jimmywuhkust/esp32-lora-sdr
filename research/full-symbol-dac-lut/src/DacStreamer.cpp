#include "DacStreamer.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "sdkconfig.h"
#endif
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
#include <Arduino.h>
#include "soc/soc.h"
#include "hal/cpu_hal.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace lora_sdr {
extern "C" void rom_set_txclk_en(unsigned);
extern "C" void rom_set_rxclk_en(unsigned);
// Reader/writer scheduling follows the 0BSD E1 experiment in esp32-sdr-trx:
// write already-read blocks, trigger the next buffer immediately at DONE,
// then fill its last 512 samples before the reader reaches that tail.
// Our samples come from a compact LoRa phase ring rather than a CW oscillator.
struct Cursor {
    const uint16_t *phase;
    const uint32_t *lookup;
    const uint32_t *down;
    const DacSegment *plan;
    unsigned ringLength,segments,segment=0,position=0;
};

static void IRAM_ATTR __attribute__((optimize("O3"))) expand(uint32_t* destination,
                 const uint16_t* phase,const uint32_t* lut,unsigned count) {
    unsigned groups=count/4;
    if(groups) {
        const uint16_t* source=phase;uint32_t* output=destination;unsigned iterations=groups;
        // Pre-scaled 16-bit LUT offsets remove four shifts per iteration.
        // Separate the loads/address adds/lookup loads/stores to hide load-use
        // latency. No flash or PSRAM accesses occur inside this timed loop.
        __asm__ volatile(
            "loopnez %[iterations], 1f\n"
            "l16ui a4, %[source], 0\n" "l16ui a5, %[source], 2\n"
            "l16ui a6, %[source], 4\n" "l16ui a7, %[source], 6\n"
            "add a4, %[table], a4\n" "add a5, %[table], a5\n"
            "add a6, %[table], a6\n" "add a7, %[table], a7\n"
            "l32i a8, a4, 0\n" "l32i a9, a5, 0\n"
            "l32i a10, a6, 0\n" "l32i a11, a7, 0\n"
            "s32i a8, %[output], 0\n" "s32i a9, %[output], 4\n"
            "s32i a10, %[output], 8\n" "s32i a11, %[output], 12\n"
            "addi %[source], %[source], 8\n" "addi %[output], %[output], 16\n"
            "1:\n"
            : [source] "+a"(source),[output] "+a"(output),[iterations] "+a"(iterations)
            : [table] "a"(lut)
            : "a4","a5","a6","a7","a8","a9","a10","a11","memory");
    }
    for(unsigned k=groups*4;k<count;k++)destination[k]=lut[phase[k]>>2];
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
    cursor.phase=reinterpret_cast<const uint16_t*>(phase);cursor.down=down;cursor.lookup=lookup;cursor.plan=plan;
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
bool IRAM_ATTR dacLutSelfTest(unsigned& maximumCycles) {
    uint16_t phase[260];uint32_t table[256],output[260];maximumCycles=0;
    for(unsigned i=0;i<256;i++)table[i]=((i*37u)&1023u)|(((i*61u)&1023u)<<10);
    for(unsigned i=0;i<260;i++)phase[i]=((i*73u+19u)&255u)*4;
    for(unsigned start=0;start<4;start++)for(unsigned count=1;count<=256;count++) {
        for(unsigned i=0;i<260;i++)output[i]=0xa5a55a5a;
        uint32_t before=cpu_hal_get_cycle_count();expand(output+1,phase+start,table,count);
        unsigned cycles=cpu_hal_get_cycle_count()-before;
        if(count==256&&cycles>maximumCycles)maximumCycles=cycles;
        if(output[0]!=0xa5a55a5a||output[count+1]!=0xa5a55a5a)return false;
        for(unsigned i=0;i<count;i++)if(output[i+1]!=table[phase[start+i]>>2])return false;
    }
    return true;
}
// Independent digital SRAM readback experiment. Never keys the RF chain.
// Paired idle/playing cases use identical addresses, pattern and writer.
bool IRAM_ATTR dacBankSelfTest(bool playing,unsigned& bad,unsigned& first,unsigned& maximumCycles) {
    bad=0;first=16384;maximumCycles=0;
    uint16_t* phase=static_cast<uint16_t*>(heap_caps_malloc(32768+1024,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!phase)return false;
    uint32_t* table=reinterpret_cast<uint32_t*>(phase+16384);
    for(unsigned i=0;i<256;i++)table[i]=((i*37u)&1023u)|(((i*61u)&1023u)<<10);
    for(unsigned i=0;i<16384;i++)phase[i]=((i*73u+19u)&255u)*4;
    uint32_t* bank=reinterpret_cast<uint32_t*>(0x3fcd0000);
    for(unsigned i=0;i<16384;i++)bank[i]=0;
    __asm__ volatile("memw" ::: "memory");
    rom_set_txclk_en(1);rom_set_rxclk_en(1);
    uint32_t owner=REG_READ(0x600c101c),saved=REG_READ(0x60033d64);
    REG_WRITE(0x600c101c,(owner&~15u)|4u);
    unsigned irq=portSET_INTERRUPT_MASK_FROM_ISR();
    if(playing){REG_WRITE(0x60033d64,16383);REG_WRITE(0x60033d64,16383|0x80000000u);}
    uint32_t started=cpu_hal_get_cycle_count();
    for(unsigned w=0;w<16384;w+=256) {
        if(playing) {
            uint32_t target=started+6u*(w+255+64);
            while(static_cast<int32_t>(cpu_hal_get_cycle_count()-target)<0){}
        }
        uint32_t before=cpu_hal_get_cycle_count();expand(bank+w,phase+w,table,256);
        unsigned cycles=cpu_hal_get_cycle_count()-before;if(cycles>maximumCycles)maximumCycles=cycles;
    }
    bool ok=true;
    if(playing) {
        uint32_t deadline=cpu_hal_get_cycle_count()+240000;
        while(!(REG_READ(0x60033d64)&(1u<<18))&&static_cast<int32_t>(cpu_hal_get_cycle_count()-deadline)<0){}
        ok=(REG_READ(0x60033d64)&(1u<<18))!=0;
    }
    REG_WRITE(0x60033d64,0);portCLEAR_INTERRUPT_MASK_FROM_ISR(irq);
    for(unsigned i=0;i<16384;i++)if(reinterpret_cast<volatile uint32_t*>(bank)[i]!=table[phase[i]>>2]) {
        if(!bad)first=i;bad++;
    }
    REG_WRITE(0x60033d64,saved&~0x80000000u);REG_WRITE(0x600c101c,owner);
    heap_caps_free(phase);return ok;
}
}
#else
namespace lora_sdr {
void streamDac(const uint8_t*,const uint32_t*,const uint32_t*,unsigned,const DacSegment*,unsigned,unsigned,DacStreamStats& stats){stats.timedOut=true;}
}
#endif
