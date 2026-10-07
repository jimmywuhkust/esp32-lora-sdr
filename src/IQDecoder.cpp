// Native finite-window LoRa demodulator. Synchronization and folded FFT
// strategy follow jkadbear/LoRaPHY (MIT) and the lora-phy 0.3.0 translation;
// see THIRD_PARTY.md for attribution and source provenance.
// Radix-2 FFT, 8x interpolation, bounded hypotheses, and packet codec are C++.
#include "PacketDecoder.h"
#include "RxFilter.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#ifdef LORA_SDR_DIAGNOSTIC_TRACE
#include <cstdio>
#endif
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
namespace lora_sdr {
struct Complex {float re,im;};
static Complex mul(Complex a,Complex b){return {a.re*b.re-a.im*b.im,a.re*b.im+a.im*b.re};}
static Complex add(Complex a,Complex b){return {a.re+b.re,a.im+b.im};}
static float magnitude(Complex a){return sqrtf(a.re*a.re+a.im*a.im);}
static constexpr float pi=3.14159265358979323846f;
static void fft(Complex* x,unsigned n) {
    for(unsigned i=1,j=0;i<n;i++){
        unsigned bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j)std::swap(x[i],x[j]);
    }
    for(unsigned len=2;len<=n;len*=2){
        Complex step={cosf(-2*pi/len),sinf(-2*pi/len)};
        for(unsigned base=0;base<n;base+=len){Complex w={1,0};
            for(unsigned j=0;j<len/2;j++){
                Complex a=x[base+j],b=mul(x[base+j+len/2],w);
                x[base+j]=add(a,b);x[base+j+len/2]={a.re-b.re,a.im-b.im};w=mul(w,step);
            }
        }
    }
}
struct Demod {
    const Complex* signal;size_t samples;unsigned n,bins,fftLength,chips;
    Complex *work,*up;float cfo=0;bool coherent=false;unsigned calls=0;
    struct Peak {float height;int bin;bool valid;};
    Peak dechirp(int start,bool isUp=true,bool coarse=false){
        if(start<0||static_cast<size_t>(start)+n>samples)return {0,0,false};
        Complex step={cosf(-2*pi*cfo/406250),sinf(-2*pi*cfo/406250)},w={1,0};
        for(unsigned j=0;j<n;j++){
            Complex ch=up[j];if(isUp)ch.im=-ch.im;
            work[j]=mul(signal[start+j],ch);
            if(cfo){work[j]=mul(work[j],w);w=mul(w,step);}
        }
        unsigned size=coarse?n:fftLength,folded=coarse?chips:bins;
        if(!coarse)memset(work+n,0,(fftLength-n)*sizeof(Complex));
        fft(work,size);
        float best=0;int bin=0;
        for(unsigned j=0;j<folded;j++){
            Complex a=work[j],b=work[size-folded+j];
            float power=coherent?magnitude(add(a,b)):magnitude(a)+magnitude(b);
            if(power>best){best=power;bin=j;}
        }
#ifdef ESP_PLATFORM
        if((++calls&15)==0)vTaskDelay(1);
#endif
        return {best,bin*(coarse?8:1),best>0};
    }
    float symbolValue(int bin,int ref)const {
        int v=(bin+int(bins)-ref)%int(bins);if(v<0)v+=bins;
        return v/8.0f;
    }
    int symbol(int bin,int ref)const {
        return static_cast<int>(lroundf(symbolValue(bin,ref)))%chips;
    }
    void bitConfidence(int ref,float drift,float* output)const {
        float zero[7]={},one[7]={};
        for(unsigned symbol=0;symbol<128;symbol++) {
            int bin=int(lroundf(ref+(symbol+drift)*8))%int(bins);if(bin<0)bin+=bins;
            Complex a=work[bin],b=work[fftLength-bins+bin];
            float power=coherent?magnitude(add(a,b)):magnitude(a)+magnitude(b);
            unsigned gray=(symbol-1)&127;gray^=gray>>1;
            for(unsigned bit=0;bit<7;bit++) {
                float& best=(gray&(1u<<bit))?one[bit]:zero[bit];if(power>best)best=power;
            }
        }
        for(unsigned bit=0;bit<7;bit++)output[bit]=(one[bit]-zero[bit])/(one[bit]+zero[bit]+1e-9f);
    }
    int detect(int start) {
        int previous=-1;unsigned run=0;
        while(start>=0&&static_cast<size_t>(start)+6*n+1<samples){
            if(run==5){
                // Refine timing/CFO with the original padded FFT only once
                // a stable preamble exists. Noise windows need no 8x FFT.
                Peak fine=dechirp(start-int(n));
                return fine.valid?start-static_cast<int>(lroundf(fine.bin/4.0f)):-1;
            }
            Peak p=dechirp(start,true,true);if(!p.valid)return -1;
            int delta=previous<0?int(bins):abs(previous-p.bin);delta=std::min(delta,int(bins)-delta);
            run=(previous>=0&&delta<=8)?run+1:1;previous=p.bin;start+=n;
        }return -1;
    }
    bool sync(int detected,int& start,int& ref,float& offset){
        start=detected;bool found=false;
        for(unsigned k=0;k<80&&start>=0&&static_cast<size_t>(start)+n<samples;k++){
            Peak u=dechirp(start),d=dechirp(start,false);start+=n;
            if(d.height>u.height){found=true;break;}
        }
        if(!found)return false;
        Peak d=dechirp(start,false);if(!d.valid)return false;
        int signedBin=d.bin+1>int(bins)/2?d.bin-int(bins):d.bin;
        start+=static_cast<int>(lroundf(signedBin/8.0f));
        Peak p=dechirp(start-4*int(n));if(!p.valid)return false;ref=p.bin;
        offset=(ref+1>int(bins)/2?ref-int(bins):ref)*203125.0f/bins;
        Peak u=dechirp(start-int(n)),down=dechirp(start-int(n),false);
        if(!u.valid||!down.valid)return false;
        start+=u.height>down.height?9*n/4:5*n/4;
        return true;
    }
};
bool decodeIQ(const int16_t* iq,size_t count,const Config& c,PacketCallback callback,void* context,
              RxStatistics& stats,uint64_t firstSample,unsigned maxPackets){
    stats=RxStatistics{};
    if(!iq||!callback||!count||count>250000||c.bandwidthHz!=203125||
       c.spreadingFactor<7||c.spreadingFactor>12||!c.explicitHeader)return false;
    size_t samples=(count*13+7)/8;
    Complex* filtered=static_cast<Complex*>(malloc(count*sizeof(Complex)));
    Complex* signal=static_cast<Complex*>(malloc(samples*sizeof(Complex)));
    unsigned n=2u<<c.spreadingFactor,fftLength=n*8;
    Complex* work=static_cast<Complex*>(malloc(fftLength*sizeof(Complex)));
    Complex* up=static_cast<Complex*>(malloc(n*sizeof(Complex)));
    uint16_t* rawBins=static_cast<uint16_t*>(malloc(1100*sizeof(uint16_t)));
    float* confidence=c.spreadingFactor==7?static_cast<float*>(malloc(1100*7*sizeof(float))):nullptr;
    if(!filtered||!signal||!work||!up||!rawBins){free(filtered);free(signal);free(work);free(up);free(rawBins);free(confidence);return false;}
    Complex state[5][2]={};
    for(size_t i=0;i<count;i++){
        Complex x={float(iq[2*i]),float(iq[2*i+1])};
        for(unsigned j=0;j<5;j++){
            const float* s=rxSos[j];Complex out={s[0]*x.re+state[j][0].re,s[0]*x.im+state[j][0].im};
            state[j][0]={s[1]*x.re-s[4]*out.re+state[j][1].re,s[1]*x.im-s[4]*out.im+state[j][1].im};
            state[j][1]={s[2]*x.re-s[5]*out.re,s[2]*x.im-s[5]*out.im};x=out;
        }filtered[i]=x;
    }
    // Centered polyphase interpolation: same 261-tap Kaiser filter as
    // scipy resample_poly(13,8), evaluated entirely on the microcontroller.
    for(size_t i=0;i<samples;i++){
        int time=static_cast<int>(i*8)+130,lo=(time-260+12)/13,hi=time/13;
        Complex out={0,0};
        for(int k=lo;k<=hi;k++)if(k>=0&&static_cast<size_t>(k)<count){
            int h=time-13*k;if(h>=0&&h<=260){out.re+=filtered[k].re*rxResample[h];out.im+=filtered[k].im*rxResample[h];}
        }signal[i]=out;
    }free(filtered);
    for(unsigned i=0;i<n;i++){
        double chip=i*.5,N=1u<<c.spreadingFactor,phase=3.141592653589793*(chip*chip/N-chip);
        up[i]={static_cast<float>(cos(phase)),static_cast<float>(sin(phase))};
    }
    Demod rx{};rx.signal=signal;rx.samples=samples;rx.n=n;rx.bins=n*4;
    rx.fftLength=fftLength;rx.chips=1u<<c.spreadingFactor;rx.work=work;rx.up=up;
    const int timings[5]={0,-2,1,0,-2};
    uint64_t accepted[32]={};unsigned acceptedCount=0;
    bool magnitudeCandidate=false,coherentCandidate=false;
    for(unsigned hypothesis=0;hypothesis<5&&(!maxPackets||stats.packets<maxPackets);hypothesis++){
        // Timing adjustments happen after detection. If a fold found no
        // preamble, its other timing hypotheses would repeat the same scan.
        if((hypothesis==1||hypothesis==2)&&!magnitudeCandidate)continue;
        if(hypothesis==4&&!coherentCandidate)continue;
        int cursor=0;rx.coherent=hypothesis>=3;
        while(cursor>=0&&static_cast<size_t>(cursor)+8*n<=samples&&(!maxPackets||stats.packets<maxPackets)){
            rx.cfo=0;int detected=rx.detect(cursor);if(detected<0)break;stats.candidates++;
            if(rx.coherent)coherentCandidate=true;else magnitudeCandidate=true;
            int start=0,ref=0;float offset=0;
            if(!rx.sync(detected,start,ref,offset)){cursor=std::max(cursor+int(n),detected+int(n));continue;}
            start+=timings[hypothesis];
            if(rx.coherent){rx.cfo=offset;auto p=rx.dechirp(start-25*n/4);if(!p.valid){cursor=detected+n;continue;}ref=p.bin;}
            else {ref=(ref+timings[hypothesis]*4+int(rx.bins))%rx.bins;}
            if(start<cursor||start<0||static_cast<size_t>(start)+8*n>samples){cursor=std::max(cursor+int(n),detected+int(n));continue;}
            // Retain FFT bins until drift compensation. Rounding a symbol
            // first discards the fractional peak needed by long packets.
            uint16_t symbols[1100];bool complete=true;
            for(unsigned j=0;j<8;j++){
                auto p=rx.dechirp(start+j*n);rawBins[j]=p.bin;
                symbols[j]=rx.symbol(p.bin,ref);
            }
            RxPacket packet;
            if(!PacketDecoder::header(symbols,c,packet)){cursor=std::max(cursor+int(n),start+7*int(n));continue;}
            stats.headers++;size_t total=PacketDecoder::symbolCount(packet,c);
            if(total>1100||static_cast<size_t>(start)+total*n>samples){cursor=std::max(cursor+int(n),start+7*int(n));continue;}
            int targets[2]={(c.syncWord>>4)*8,(c.syncWord&15)*8};bool syncOk=true;
            for(unsigned j=0;j<2;j++){
                auto p=rx.dechirp(start-(17-4*j)*n/4);if(!p.valid){syncOk=false;break;}
                int v=rx.symbol(p.bin,ref),delta=abs(v-targets[j]);delta=std::min(delta,int(rx.chips)-delta);
                if(delta>1)syncOk=false;
            }
            if(!syncOk){stats.syncRejected++;cursor=start+total*n;continue;}
            for(size_t j=8;j<total;j++){
                auto p=rx.dechirp(start+j*n);if(!p.valid){complete=false;break;}rawBins[j]=p.bin;
                if(confidence)rx.bitConfidence(ref,(j+2)*float(rx.chips)*offset/c.frequencyHz,confidence+j*7);
            }
            // Keep fractional CFO/SFO correction until nearest-integer rounding.
            // A second, quantized-peak hypothesis preserves the earlier path
            // for distorted folded peaks. Both must pass the entire CRC; the
            // receiver receives no expected bytes or payload-specific hint.
            bool ldro=(rx.chips*1000ull>c.bandwidthHz*16ull),decoded=false;
            if(complete)for(unsigned variant=0;variant<2&&!decoded;variant++){
                double binOffset=0,last=1;
                for(size_t j=0;j<total;j++){
                    double raw=variant?rx.symbol(rawBins[j],ref):rx.symbolValue(rawBins[j],ref);
                    double v=fmod(raw-(j+2)*double(rx.chips)*offset/c.frequencyHz+rx.chips,rx.chips);
                    if(ldro){double delta=fmod(v-last+rx.chips,4);binOffset-=delta<2?delta:delta-4;last=v;v=fmod(v+binOffset+rx.chips,rx.chips);}
                    symbols[j]=static_cast<unsigned>(lround(v))%rx.chips;
                }
                decoded=PacketDecoder::decode(symbols,total,c,packet);
            }
            if(complete&&!decoded&&confidence)decoded=PacketDecoder::decodeSoft(symbols,total,confidence,c,packet);
            if(decoded){
                packet.frequencyOffsetHz=offset;packet.sampleIndex=firstSample+(start*8ull+6)/13;
                bool duplicate=false;
                for(unsigned j=0;j<acceptedCount;j++)if(llabs(static_cast<int64_t>(packet.sampleIndex-accepted[j]))<int64_t(rx.chips*250000ull/203125))duplicate=true;
                if(!duplicate){if(acceptedCount<32)accepted[acceptedCount++]=packet.sampleIndex;stats.packets++;callback(packet,context);}
            }else {
                stats.crcRejected++;
#ifdef LORA_SDR_DIAGNOSTIC_TRACE
                printf("TRACE rejected hypothesis=%u start=%d ref=%d cfo=%.1f bytes=%u cr=%u crc=%04x hex=",
                    hypothesis,start,ref,offset,unsigned(packet.length),packet.codingRate,packet.crc);
                for(size_t j=0;j<packet.length;j++)printf("%02x",packet.payload[j]);
                printf(" symbols=");for(size_t j=0;j<total;j++)printf("%u,",symbols[j]);printf("\n");
#endif
            }
            cursor=start+total*n;
        }
    }
    free(signal);free(work);free(up);free(rawBins);free(confidence);return true;
}
}
