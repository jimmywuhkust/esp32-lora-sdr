#include "ESP32S3Radio.h"
#if defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)
#include "sdkconfig.h"
#endif
#if (defined(ARDUINO_ARCH_ESP32) || defined(ESP_PLATFORM)) && defined(CONFIG_IDF_TARGET_ESP32S3)
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include <WiFi.h>
#include "hal/cpu_hal.h"
#include "driver/periph_ctrl.h"
#else
#include "esp_cpu.h"
#include "esp_private/esp_clk.h"
#include "esp_private/periph_ctrl.h"
#define cpu_hal_get_cycle_count esp_cpu_get_cycle_count
extern "C" int lora_sdr_platform_begin(void);
#endif
#include <math.h>
#include <algorithm>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "esp_phy_init.h"
#include "esp_heap_caps.h"
#include "heap_memory_layout.h"
#if defined(LORA_SDR_NATIVE_BACKEND) && defined(ARDUINO_ARCH_ESP32)
extern "C" int lora_sdr_platform_begin(void);
#endif
#include "soc/soc.h"
#include "soc/system_reg.h"
#include "soc/syscon_reg.h"
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
void rom_pbus_xpd_rx_off(void);
void rom_set_txclk_en(unsigned);
void rom_set_rxclk_en(unsigned);
extern char _bss_end[],_data_end[],_iram_end[];
}
#if defined(ARDUINO_ARCH_ESP32) && !defined(LORA_SDR_NATIVE_BACKEND)
SOC_RESERVE_MEMORY_REGION(0x3fcd0000,0x3fce0000,s3_dac_bank);
#endif
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
static bool restoreAnalogGain(TxResult& result,uint32_t control) {
    bool a=pbusWrite(5,1,result.analogBefore1),b=pbusWrite(5,3,result.analogBefore3);
    bool restored=a&&b&&(rom_pbus_rd(5,1)&511u)==result.analogBefore1&&
        (rom_pbus_rd(5,3)&511u)==result.analogBefore3;
    REG_WRITE(0x60006104,control);result.analogGainRestored=restored;return restored;
}
static unsigned dacCopyCycles;
static bool dacTimedOut;
static void freeDacSource(uint32_t* ring) {
#if defined(ESP_PLATFORM) && (!defined(ARDUINO_ARCH_ESP32) || defined(LORA_SDR_NATIVE_BACKEND))
    if(reinterpret_cast<uintptr_t>(ring)==0x3fcb0000u)return;
#endif
    heap_caps_free(ring);
}
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
#if !defined(LORA_SDR_NATIVE_BACKEND)
        if(segment&&segment%16==0) {
            portCLEAR_INTERRUPT_MASK_FROM_ISR(irq);
            esp_rom_delay_us(8);
            irq=portSET_INTERRUPT_MASK_FROM_ISR();
        }
#endif
        // The supplied native/ArduinoDuplex profiles disable watchdogs for
        // bounded acquisition. Keep this bounded TX atomic there too: a USB
        // ISR or task switch during a header chirp can exceed its RF deadline.
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
#if defined(ARDUINO_ARCH_ESP32)
    if(getCpuFrequencyMhz()!=240)return Error::Unsupported;
#if defined(LORA_SDR_NATIVE_BACKEND)
    if(lora_sdr_platform_begin()!=0)return Error::NotReady;
#endif
#else
    if(esp_clk_cpu_freq()!=240000000)return Error::Unsupported;
    if(lora_sdr_platform_begin()!=0)return Error::NotReady;
#endif
    if(reinterpret_cast<uintptr_t>(_bss_end)>0x3fcd0000u||reinterpret_cast<uintptr_t>(_data_end)>0x3fcd0000u||
       reinterpret_cast<uintptr_t>(_iram_end)>0x403c0000u)return Error::Unsupported;
#if defined(ARDUINO_ARCH_ESP32) && !defined(LORA_SDR_NATIVE_BACKEND)
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
#endif
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
    if(c.analogGainCode>63||(c.analogGainCode&&c.transport!=Transport::DacWindows))return Error::Unsupported;
    if(c.frequencyHz<2400200000u||c.frequencyHz>2483300000u||
       (c.bandwidthHz!=203125&&c.bandwidthHz!=406250&&c.bandwidthHz!=812500)||c.spreadingFactor<7||c.spreadingFactor>9||
       c.preambleSymbols<12||c.preambleSymbols>64||c.frequencyCorrectionHz<-50000||c.frequencyCorrectionHz>50000||
       c.gainCode<64||c.gainCode>200||!c.explicitHeader||!c.payloadCrc||
       c.updateRateHz<40000||c.updateRateHz>200000||240000000u%c.updateRateHz)return Error::Unsupported;
    uint16_t symbols[1024];
    Error status=Encoder::encode(data,length,c,symbols,1024,result.packet);
    if(status!=Error::Ok)return status;
#if defined(ESP_PLATFORM) && (!defined(ARDUINO_ARCH_ESP32) || defined(LORA_SDR_NATIVE_BACKEND))
    // Restore the SDK channel state before entering the TX test mode.
    if(esp_wifi_set_channel(1,WIFI_SECOND_CHAN_NONE)!=ESP_OK)return Error::NotReady;
    rom_pbus_xpd_rx_off();
#endif
    if(c.transport==Transport::DacWindows) {
        if(c.dacAmplitude<1||c.dacAmplitude>200||c.dacWindowSamples<1000||
           c.dacWindowSamples>16380||result.packet.airtimeMs>1000)return Error::Unsupported;
        unsigned ringLength=static_cast<unsigned>(round((1u<<c.spreadingFactor)*40000000.0/c.bandwidthHz));
        if(c.dacWindowSamples>=ringLength)return Error::Unsupported;
        unsigned downLength=(static_cast<uint64_t>(6000)*ringLength+12603)/25206;
        if(downLength>c.dacWindowSamples)downLength=c.dacWindowSamples;
        bool compact=ringLength>25206;
        unsigned ringBytes=(ringLength+downLength)*(compact?1:4),lookupOffset=(ringBytes+3)&~3u;
        uint32_t *ring;
#if defined(ESP_PLATFORM) && (!defined(ARDUINO_ARCH_ESP32) || defined(LORA_SDR_NATIVE_BACKEND))
        // Half-duplex: receive is stopped. Reuse reserved RF banks 0 and 1
        // as the waveform source; bank 2 alone belongs to the DAC engine.
        // This keeps the proven SF7 32-bit waveform without a 125 KiB heap
        // allocation in a firmware that also reserves the three RX banks.
        if(lookupOffset+(compact?1024u:0u)>0x20000u)return Error::NoMemory;
        REG_CLR_BIT(0x600c101c,3u);
        ring=reinterpret_cast<uint32_t*>(0x3fcb0000u);
#else
        ring=static_cast<uint32_t*>(heap_caps_malloc(lookupOffset+(compact?1024:0),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
#endif
        if(!ring)return Error::NoMemory;
        result.sourceAddress=reinterpret_cast<uintptr_t>(ring);
        uint32_t *lookup=compact?reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ring)+lookupOffset):nullptr;
        const uint32_t *down=compact?reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(ring)+ringLength):ring+ringLength;
        int64_t generationStarted=esp_timer_get_time();
        if(compact)for(unsigned j=0;j<256;j++) {
            float angle=j*6.283185307179586f/256;
            int i=lroundf(c.dacAmplitude*cosf(angle)),q=lroundf(c.dacAmplitude*sinf(angle));
            lookup[j]=(static_cast<unsigned>(i)&1023u)|((static_cast<unsigned>(q)&1023u)<<10);
        }
        for(unsigned j=0;j<ringLength;j++) {
            // The S3 has a single-precision FPU. Software double trig delayed
            // RF by ~0.7 s, past a peer's 500 ms receive window. Float phase
            // stays much finer than the 10-bit DAC resolution at these SFs.
            float chip=j*(c.bandwidthHz/40000000.0f),n=1u<<c.spreadingFactor;
            float phase=3.141592653589793f*(chip*chip/n-chip)*(c.inverted?-1.0f:1.0f);
            if(compact) {
                auto *phases=reinterpret_cast<uint8_t*>(ring);
                phases[j]=static_cast<uint8_t>(lroundf(phase*256/6.283185307179586f));
                if(j<downLength)phases[ringLength+j]=static_cast<uint8_t>(-phases[j]);
            } else {
                int i=lroundf(c.dacAmplitude*cosf(phase)),q=lroundf(c.dacAmplitude*sinf(phase));
                ring[j]=(static_cast<unsigned>(i)&1023u)|((static_cast<unsigned>(q)&1023u)<<10);
                if(j<downLength)ring[ringLength+j]=(static_cast<unsigned>(i)&1023u)|((static_cast<unsigned>(-q)&1023u)<<10);
            }
        }
        result.waveformBuildUs=esp_timer_get_time()-generationStarted;
        txcal_debuge_mode();start_tx_tone_step(1,0,c.gainCode,0,0,0);
        uint32_t pbusControl=0;
        if(c.analogGainCode) {
            pbusControl=REG_READ(0x60006104);
            result.analogBefore1=rom_pbus_rd(5,1)&511u;
            result.analogBefore3=rom_pbus_rd(5,3)&511u;
            bool valid=c.analogGainCode<=result.analogBefore1&&c.analogGainCode<=result.analogBefore3;
            if(valid) {
                REG_SET_BIT(0x60006104,1u);
                bool a=pbusWrite(5,1,c.analogGainCode),b=pbusWrite(5,3,c.analogGainCode);
                valid=a&&b;
            }
            result.analogAfter1=rom_pbus_rd(5,1)&511u;
            result.analogAfter3=rom_pbus_rd(5,3)&511u;
            valid=valid&&result.analogAfter1==c.analogGainCode&&result.analogAfter3==c.analogGainCode;
            if(!valid) {
                restoreAnalogGain(result,pbusControl);
                stop_tx_tone(1);txcal_work_mode();rom_pbus_xpd_tx_off();freeDacSource(ring);return Error::NotReady;
            }
        }
        rom_set_txclk_en(1);
        rom_set_rxclk_en(1);
        int64_t frequency=static_cast<int64_t>(c.frequencyHz)+c.frequencyCorrectionHz;
        unsigned khz=static_cast<unsigned>(frequency/1000);set_rf_freq_offset(0,khz/1000,khz%1000);
        // The SDK can update peripheral gates after begin(), including while
        // an Arduino application yields. Acquire the DAC/MAC clock at the
        // point of use rather than relying on its startup state.
        result.playbackClockBefore=REG_READ(SYSTEM_WIFI_CLK_EN_REG);
        periph_module_enable(PERIPH_WIFI_MODULE);
        REG_SET_BIT(SYSTEM_WIFI_CLK_EN_REG,1u<<6);
        result.playbackClockEnabled=REG_READ(SYSTEM_WIFI_CLK_EN_REG);
        uint32_t owner=REG_READ(0x600c101c),saved=REG_READ(0x60033d64),tone=REG_READ(0x60006040);
        result.basebandControl=tone;result.adcControl=REG_READ(0x60033d5c);
        result.keyedGain1=rom_pbus_rd(5,1);result.keyedGain3=rom_pbus_rd(5,3);
        REG_WRITE(0x600c101c,(owner&~15u)|4u);REG_WRITE(0x60006040,tone&~(1u<<18));
        Config playback=c;
#if defined(LORA_SDR_NATIVE_BACKEND)
        // Measure the actual SRAM bus cost before scheduling RF deadlines.
        // Arduino USB / acquisition context made a 15000-sample copy take
        // 301 us on this board, exceeding its 255 us idle interval. Choose
        // a shorter window with 34 us margin instead of drifting every chirp.
        if(c.spreadingFactor==7) {
            unsigned measured=0;
            for(unsigned trial=0;trial<3;trial++) {
                unsigned irq=portSET_INTERRUPT_MASK_FROM_ISR();
                uint32_t begun=cpu_hal_get_cycle_count();
                stageDac(reinterpret_cast<uint32_t*>(0x3fcd0000),ring,lookup,trial*123,
                         c.dacWindowSamples,ringLength);
                unsigned elapsed=cpu_hal_get_cycle_count()-begun;
                portCLEAR_INTERRUPT_MASK_FROM_ISR(irq);
                if(elapsed>measured)measured=elapsed;
            }
            result.preflightCopyCycles=measured;
            // CPU 240 MHz / DAC 40 MHz = 6 CPU cycles per played sample.
            uint64_t budget=ringLength*6u>8192?ringLength*6u-8192:0;
            unsigned safe=budget*c.dacWindowSamples/(measured+6u*c.dacWindowSamples);
            if(safe<c.dacWindowSamples)playback.dacWindowSamples=std::max(1000u,safe/100*100);
        }
#endif
        result.playbackWindowSamples=playback.dacWindowSamples;
        result.lateUpdates=playDac(ring,down,lookup,symbols,result.packet.symbolCount,&playback,ringLength,downLength);
        result.maxCopyCycles=dacCopyCycles;
        result.updates=c.preambleSymbols+5+result.packet.symbolCount;
        REG_WRITE(0x60033d64,saved&~0x80000000u);REG_WRITE(0x600c101c,owner);REG_WRITE(0x60006040,tone);
        bool restored=!c.analogGainCode||restoreAnalogGain(result,pbusControl);
        stop_tx_tone(1);txcal_work_mode();rom_pbus_xpd_tx_off();freeDacSource(ring);
        if(!restored)return Error::NotReady;
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
