#include "ESP32S3Radio.h"
#if defined(ARDUINO_ARCH_ESP32)
#include "sdkconfig.h"
#endif
#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_IDF_TARGET_ESP32S3)
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_phy_init.h"
#include "esp_heap_caps.h"
#include "hal/cpu_hal.h"
#include "heap_memory_layout.h"
#include "soc/soc.h"
#include "soc/system_reg.h"
#include "soc/syscon_reg.h"
#include "driver/periph_ctrl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
extern "C" {
unsigned rom_chip_i2c_readReg(unsigned,unsigned,unsigned);
unsigned rom_pbus_rd(unsigned,unsigned);
void rom_chip_i2c_writeReg(unsigned,unsigned,unsigned,unsigned);
void set_rf_freq_offset(unsigned,unsigned,int);
void start_tx_tone_step(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned);
void stop_tx_tone(unsigned);
void txcal_debuge_mode(void);
void txcal_work_mode(void);
void rom_pbus_xpd_tx_off(void);
void rom_set_txclk_en(unsigned);
void rom_set_rxclk_en(unsigned);
extern char _bss_end[],_data_end[],_iram_end[];
}
SOC_RESERVE_MEMORY_REGION(0x3fcd0000,0x3fce0000,s3_dac_bank);
namespace lora_sdr {
static bool pbusWrite(unsigned block,unsigned index,unsigned value) {
    unsigned fields=((value&511u)<<6)|((block&15u)<<2)|((index&3u)<<15);
    REG_WRITE(0x60006104,(REG_READ(0x60006104)&0xfffe0001u)|(fields&0x1fffcu)|2u);
    uint32_t started=cpu_hal_get_cycle_count();
    while(REG_READ(0x60006110)&0x80000000u) {
        if(cpu_hal_get_cycle_count()-started>24000u){REG_CLR_BIT(0x60006104,2u);return false;}
    }
    REG_CLR_BIT(0x60006104,2u);return true;
}
static unsigned dacCopyCycles;
static bool dacTimedOut;
static void IRAM_ATTR __attribute__((optimize("O3"))) copyDac(uint32_t *destination,
                      const uint32_t *ring,unsigned offset,unsigned length,unsigned ringLength) {
    unsigned copied=0;
    while(copied<length) {
        unsigned span=ringLength-offset;if(span>length-copied)span=length-copied;
        const uint32_t *src=ring+offset;uint32_t *out=destination+copied;unsigned k=0;
        for(;k+4<=span;k+=4) {
            uint32_t a=src[k],b=src[k+1],c=src[k+2],d=src[k+3];
            out[k]=a;out[k+1]=b;out[k+2]=c;out[k+3]=d;
        }
        for(;k<span;k++)out[k]=src[k];
        copied+=span;offset=0;
    }
    __asm__ volatile("memw" ::: "memory");
}
static void IRAM_ATTR __attribute__((optimize("O3"))) stageDac(uint32_t *destination,
                      const uint32_t *ring,const uint32_t *lookup,unsigned offset,unsigned length,unsigned ringLength) {
    if(!lookup){copyDac(destination,ring,offset,length,ringLength);return;}
    const uint8_t *phase=reinterpret_cast<const uint8_t*>(ring);
    unsigned copied=0;
    while(copied<length) {
        unsigned span=ringLength-offset;if(span>length-copied)span=length-copied;
        const uint8_t *src=phase+offset;uint32_t *out=destination+copied;unsigned k=0;
        for(;k+4<=span;k+=4) {
            uint32_t a=lookup[src[k]],b=lookup[src[k+1]],c=lookup[src[k+2]],d=lookup[src[k+3]];
            out[k]=a;out[k+1]=b;out[k+2]=c;out[k+3]=d;
        }
        for(;k<span;k++)out[k]=lookup[src[k]];
        copied+=span;offset=0;
    }
    __asm__ volatile("memw" ::: "memory");
}
static unsigned IRAM_ATTR __attribute__((optimize("O3"))) playDac(const uint32_t* ring,const uint32_t* down,
                                const uint32_t* lookup,
                                const uint16_t* symbols,unsigned symbolCount,const Config* config,
                                unsigned ringLength,unsigned downLength) {
    uint32_t *destination=reinterpret_cast<uint32_t*>(0x3fcd0000);
    unsigned irq=portSET_INTERRUPT_MASK_FROM_ISR(),late=0;
    unsigned window=config->dacWindowSamples,n=1u<<config->spreadingFactor;
    unsigned quarterWindow=(static_cast<uint64_t>(1000)*ringLength+12603)/25206;
    if(quarterWindow>window)quarterWindow=window;
    uint32_t next=cpu_hal_get_cycle_count()+window*12+2400;
    unsigned segments=config->preambleSymbols+5+symbolCount;
    dacCopyCycles=0;dacTimedOut=false;
    for(unsigned segment=0;segment<segments;segment++) {
        // Arduino's SDK interrupt watchdog defaults to ~300 ms. Service
        // pending ticks in a bounded idle gap; never mask an entire long
        // packet. The absolute RF deadlines keep running during this pause.
        if(segment&&segment%16==0) {
            portCLEAR_INTERRUPT_MASK_FROM_ISR(irq);
            esp_rom_delay_us(8);
            irq=portSET_INTERRUPT_MASK_FROM_ISR();
        }
        unsigned symbol=0;bool descending=false,quarter=false;
        if(segment==config->preambleSymbols)symbol=(config->syncWord>>4)*8;
        else if(segment==config->preambleSymbols+1)symbol=(config->syncWord&15)*8;
        else if(segment>=config->preambleSymbols+2&&segment<=config->preambleSymbols+4) {
            descending=true;quarter=segment==config->preambleSymbols+4;
        } else if(segment>config->preambleSymbols+4)symbol=symbols[segment-config->preambleSymbols-5];
        unsigned length=quarter?quarterWindow:window;
        if(descending&&length>downLength)length=downLength;
        unsigned offset=(static_cast<uint64_t>(symbol)*ringLength+n/2)/n;
        uint32_t copyStarted=cpu_hal_get_cycle_count();
        // Prestage the first header chirp in the long idle gap before the
        // quarter SFD. Its tail survives the 1000-sample SFD playback; only
        // the first 1000 samples then need copying in the short quarter gap.
        if(quarter&&symbolCount) {
            unsigned headerOffset=(static_cast<uint64_t>(symbols[0])*ringLength+n/2)/n;
            stageDac(destination,ring,lookup,headerOffset,window,ringLength);
        }
        unsigned copyLength=(segment==config->preambleSymbols+5)?quarterWindow:length;
        stageDac(destination,descending?down:ring,lookup,offset,copyLength,ringLength);
        unsigned copyCycles=cpu_hal_get_cycle_count()-copyStarted;if(copyCycles>dacCopyCycles)dacCopyCycles=copyCycles;
        if(static_cast<int32_t>(cpu_hal_get_cycle_count()-next)>0)late++;
        while(static_cast<int32_t>(cpu_hal_get_cycle_count()-next)<0){}
        REG_WRITE(0x60033d64,length-1);REG_WRITE(0x60033d64,(length-1)|0x80000000u);
        next+=quarter?(ringLength*6)/4:ringLength*6;
        uint32_t deadline=cpu_hal_get_cycle_count()+240000;
        while(!(REG_READ(0x60033d64)&(1u<<18))&&static_cast<int32_t>(cpu_hal_get_cycle_count()-deadline)<0){}
        if(!(REG_READ(0x60033d64)&(1u<<18)))dacTimedOut=true;
        REG_WRITE(0x60033d64,0);
        if(dacTimedOut)break;
    }
    while(static_cast<int32_t>(cpu_hal_get_cycle_count()-next)<0){}
    portCLEAR_INTERRUPT_MASK_FROM_ISR(irq);return late;
}
static unsigned IRAM_ATTR play(const uint32_t* words,unsigned count,unsigned period) {
    unsigned irq=portSET_INTERRUPT_MASK_FROM_ISR(),late=0;
    uint32_t next=cpu_hal_get_cycle_count(),previous=~0u;
    for(unsigned i=0;i<count;i++) {
        while(static_cast<int32_t>(cpu_hal_get_cycle_count()-next)<0){}
        if(static_cast<int32_t>(cpu_hal_get_cycle_count()-next)>static_cast<int32_t>(period/2))late++;
        unsigned w=words[i];
        if(((previous^w)>>16)&255)rom_chip_i2c_writeReg(0x63,0,3,w>>16);
        if(((previous^w)>>8)&255)rom_chip_i2c_writeReg(0x63,0,4,w>>8);
        rom_chip_i2c_writeReg(0x63,0,5,w);previous=w;next+=period;
    }
    while(static_cast<int32_t>(cpu_hal_get_cycle_count()-next)<0){}
    portCLEAR_INTERRUPT_MASK_FROM_ISR(irq);return late;
}
Error ESP32S3Radio::begin() {
    if(ready_)return Error::Ok;
    if(getCpuFrequencyMhz()!=240)return Error::Unsupported;
    if(reinterpret_cast<uintptr_t>(_bss_end)>0x3fcd0000u||reinterpret_cast<uintptr_t>(_data_end)>0x3fcd0000u||
       reinterpret_cast<uintptr_t>(_iram_end)>0x403c0000u)return Error::Unsupported;
    esp_err_t event=esp_event_loop_create_default();
    if(event!=ESP_OK&&event!=ESP_ERR_INVALID_STATE)return Error::NotReady;
    if(esp_netif_init()!=ESP_OK)return Error::NotReady;
    wifi_init_config_t wifi=WIFI_INIT_CONFIG_DEFAULT();
    wifi.static_rx_buf_num=2;wifi.dynamic_rx_buf_num=4;
    wifi.tx_buf_type=1;wifi.static_tx_buf_num=0;wifi.dynamic_tx_buf_num=4;wifi.cache_tx_buf_num=4;
    if(esp_wifi_init(&wifi)!=ESP_OK)return Error::NotReady;
    if(esp_wifi_set_storage(WIFI_STORAGE_RAM)!=ESP_OK||esp_wifi_set_mode(WIFI_MODE_NULL)!=ESP_OK||
       esp_wifi_start()!=ESP_OK||esp_wifi_set_ps(WIFI_PS_NONE)!=ESP_OK||
       esp_wifi_set_promiscuous(true)!=ESP_OK||esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE)!=ESP_OK)return Error::NotReady;
    periph_module_enable(PERIPH_WIFI_MODULE);
    // The SRAM playback engine requires MAC clock bit6 even when no Wi-Fi
    // connection or Wi-Fi stack is running. This gate is outside the public
    // Wi-Fi peripheral clock mask (confirmed in the upstream failure log).
    REG_SET_BIT(SYSTEM_WIFI_CLK_EN_REG,1u<<6);
    ready_=true;return Error::Ok;
}
Error ESP32S3Radio::transmit(const uint8_t* data,size_t length,const Config& c,TxResult& result) {
    result=TxResult{};
    if(!ready_)return Error::NotReady;
    if(c.transport!=Transport::Pll&&c.transport!=Transport::DacWindows)return Error::Unsupported;
    if(c.frequencyHz<2400200000u||c.frequencyHz>2483300000u||
       (c.bandwidthHz!=203125&&c.bandwidthHz!=406250&&c.bandwidthHz!=812500)||c.spreadingFactor<7||c.spreadingFactor>9||
       c.preambleSymbols<12||c.preambleSymbols>64||c.frequencyCorrectionHz<-50000||c.frequencyCorrectionHz>50000||
       c.gainCode<64||c.gainCode>200||!c.explicitHeader||!c.payloadCrc||
       c.updateRateHz<40000||c.updateRateHz>200000||240000000u%c.updateRateHz)return Error::Unsupported;
    uint16_t symbols[1024];
    Error status=Encoder::encode(data,length,c,symbols,1024,result.packet);
    if(status!=Error::Ok)return status;
    if(c.transport==Transport::DacWindows) {
        if(c.analogGainCode<1||c.analogGainCode>119)return Error::Unsupported;
        if(c.dacAmplitude<1||c.dacAmplitude>200||c.dacWindowSamples<1000||
           c.dacWindowSamples>16380||result.packet.airtimeMs>1000)return Error::Unsupported;
        unsigned ringLength=static_cast<unsigned>(round((1u<<c.spreadingFactor)*40000000.0/c.bandwidthHz));
        if(c.dacWindowSamples>=ringLength)return Error::Unsupported;
        unsigned downLength=(static_cast<uint64_t>(6000)*ringLength+12603)/25206;
        if(downLength>c.dacWindowSamples)downLength=c.dacWindowSamples;
        bool compact=ringLength>25206;
        unsigned ringBytes=(ringLength+downLength)*(compact?1:4),lookupOffset=(ringBytes+3)&~3u;
        uint32_t *ring=static_cast<uint32_t*>(heap_caps_malloc(lookupOffset+(compact?1024:0),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
        if(!ring)return Error::NoMemory;
        result.sourceAddress=reinterpret_cast<uintptr_t>(ring);
        uint32_t *lookup=compact?reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ring)+lookupOffset):nullptr;
        const uint32_t *down=compact?reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ring)+ringLength):ring+ringLength;
        if(compact)for(unsigned j=0;j<256;j++) {
            double angle=j*6.283185307179586/256;
            int i=lround(c.dacAmplitude*cos(angle)),q=lround(c.dacAmplitude*sin(angle));
            lookup[j]=(static_cast<unsigned>(i)&1023u)|((static_cast<unsigned>(q)&1023u)<<10);
        }
        for(unsigned j=0;j<ringLength;j++) {
            double chip=j*c.bandwidthHz/40000000.0,n=1u<<c.spreadingFactor;
            double phase=3.141592653589793*(chip*chip/n-chip)*(c.inverted?-1:1);
            if(compact) {
                auto *phases=reinterpret_cast<uint8_t*>(ring);
                phases[j]=static_cast<uint8_t>(lround(phase*256/6.283185307179586));
                if(j<downLength)phases[ringLength+j]=static_cast<uint8_t>(-phases[j]);
            } else {
                int i=lround(c.dacAmplitude*cos(phase)),q=lround(c.dacAmplitude*sin(phase));
                ring[j]=(static_cast<unsigned>(i)&1023u)|((static_cast<unsigned>(q)&1023u)<<10);
                if(j<downLength)ring[ringLength+j]=(static_cast<unsigned>(i)&1023u)|((static_cast<unsigned>(-q)&1023u)<<10);
            }
        }
        txcal_debuge_mode();start_tx_tone_step(1,0,c.gainCode,0,0,0);
        uint32_t pbusControl=REG_READ(0x60006104);
        result.analogBefore1=rom_pbus_rd(5,1)&511u;
        result.analogBefore3=rom_pbus_rd(5,3)&511u;
        bool analogOk=c.analogGainCode<=result.analogBefore1&&c.analogGainCode<=result.analogBefore3;
        if(analogOk) {
            REG_SET_BIT(0x60006104,1u);
            analogOk=pbusWrite(5,1,c.analogGainCode)&&pbusWrite(5,3,c.analogGainCode);
        }
        result.analogAfter1=rom_pbus_rd(5,1)&511u;
        result.analogAfter3=rom_pbus_rd(5,3)&511u;
        analogOk=analogOk&&result.analogAfter1==c.analogGainCode&&result.analogAfter3==c.analogGainCode;
        if(!analogOk) {
            pbusWrite(5,1,result.analogBefore1);pbusWrite(5,3,result.analogBefore3);REG_WRITE(0x60006104,pbusControl);
            stop_tx_tone(1);txcal_work_mode();rom_pbus_xpd_tx_off();heap_caps_free(ring);return Error::NotReady;
        }
        rom_set_txclk_en(1);
        rom_set_rxclk_en(1);
        int64_t frequency=static_cast<int64_t>(c.frequencyHz)+c.frequencyCorrectionHz;
        unsigned khz=static_cast<unsigned>(frequency/1000);set_rf_freq_offset(0,khz/1000,khz%1000);
        uint32_t owner=REG_READ(0x600c101c),saved=REG_READ(0x60033d64),tone=REG_READ(0x60006040);
        REG_WRITE(0x600c101c,(owner&~15u)|4u);REG_WRITE(0x60006040,tone&~(1u<<18));
        result.lateUpdates=playDac(ring,down,lookup,symbols,result.packet.symbolCount,&c,ringLength,downLength);
        result.maxCopyCycles=dacCopyCycles;
        result.updates=c.preambleSymbols+5+result.packet.symbolCount;
        REG_WRITE(0x60033d64,saved&~0x80000000u);REG_WRITE(0x600c101c,owner);REG_WRITE(0x60006040,tone);
        pbusWrite(5,1,result.analogBefore1);pbusWrite(5,3,result.analogBefore3);REG_WRITE(0x60006104,pbusControl);
        stop_tx_tone(1);txcal_work_mode();rom_pbus_xpd_tx_off();heap_caps_free(ring);
        return dacTimedOut?Error::PlaybackTimeout:Error::Ok;
    }
    unsigned total=static_cast<unsigned>(round(result.packet.airtimeMs*c.updateRateHz/1000));
    // Unlike windowed DAC, this timing loop cannot service pending interrupts.
    // Reject long PLL packets before keying RF or approaching the SDK watchdog.
    if(result.packet.airtimeMs>250||total<1||total>32768||total>c.updateRateHz)return Error::Unsupported;
    uint32_t *words=static_cast<uint32_t*>(heap_caps_malloc(total*4,MALLOC_CAP_INTERNAL|MALLOC_CAP_32BIT));
    if(!words)return Error::NoMemory;
    const unsigned n=1u<<c.spreadingFactor;
    double elapsed=0;unsigned first=0,segments=c.preambleSymbols+5+result.packet.symbolCount;
    for(unsigned segment=0;segment<segments;segment++) {
        unsigned symbol=0;bool up=true;double fraction=1;
        if(segment==c.preambleSymbols)symbol=(c.syncWord>>4)*8;
        else if(segment==c.preambleSymbols+1)symbol=(c.syncWord&15)*8;
        else if(segment>=c.preambleSymbols+2&&segment<=c.preambleSymbols+4) {
            up=false;if(segment==c.preambleSymbols+4)fraction=.25;
        } else if(segment>c.preambleSymbols+4)symbol=symbols[segment-c.preambleSymbols-5];
        double end=elapsed+fraction*n/c.bandwidthHz;
        unsigned last=static_cast<unsigned>(round(end*c.updateRateHz));if(last>total)last=total;
        for(unsigned j=first;j<last;j++) {
            double chip=fmod(((j+.5)/c.updateRateHz-elapsed)*c.bandwidthHz+symbol,n);
            if(chip<0)chip+=n;
            double offset=(chip/n-.5)*c.bandwidthHz*(up?1:-1)*(c.inverted?-1:1)+c.frequencyCorrectionHz;
            words[j]=static_cast<uint32_t>(llround((c.frequencyHz+offset-960000000.0)*65536/30000000));
        }
        elapsed=end;first=last;
    }
    txcal_debuge_mode();
    start_tx_tone_step(1,0,c.gainCode,0,0,0);
    unsigned khz=c.frequencyHz/1000;
    set_rf_freq_offset(0,khz/1000,khz%1000);
    uint32_t original=(rom_chip_i2c_readReg(0x63,0,3)<<16)|(rom_chip_i2c_readReg(0x63,0,4)<<8)|rom_chip_i2c_readReg(0x63,0,5);
    uint32_t owner=REG_READ(0x6000e0c4);REG_WRITE(0x6000e0c4,owner|(1u<<25));
    result.lateUpdates=play(words,total,240000000u/c.updateRateHz);result.updates=total;
    rom_chip_i2c_writeReg(0x63,0,3,original>>16);rom_chip_i2c_writeReg(0x63,0,4,original>>8);rom_chip_i2c_writeReg(0x63,0,5,original);
    stop_tx_tone(1);REG_WRITE(0x6000e0c4,owner);txcal_work_mode();rom_pbus_xpd_tx_off();
    heap_caps_free(words);return Error::Ok;
}
}
#else
namespace lora_sdr {
Error ESP32S3Radio::begin(){return Error::Unsupported;}
Error ESP32S3Radio::transmit(const uint8_t*,size_t,const Config&,TxResult&){return Error::Unsupported;}
}
#endif
