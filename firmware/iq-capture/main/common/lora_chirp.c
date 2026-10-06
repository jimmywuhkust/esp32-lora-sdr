/* SPDX-License-Identifier: GPL-3.0-or-later
 * 250 kS/s -> 203125 S/s (13/16), Q15 dechirp + S3 FFT.
 * Half-symbol overlapping windows detect repeated upchirps at unknown timing.
 * Bins remain timing/CFO biased: NO header, FEC, whitening or packet CRC here.
 */
#include "lora_chirp.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_rom_crc.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
extern int16_t *dsps_fft_w_table_sc16;
int s3_fft2r_sc16_rnd(int16_t *, int, int16_t *);
int s3_fft2r_sc16_rnd_stage(int16_t *, int, int16_t *, unsigned);
#define MEMW() __asm__ volatile("memw" ::: "memory")
typedef struct {
    const int16_t *xi, *xq, *h;
    uint32_t groups, nout, step, shift;
    int16_t *oi, *oq;
} fir_job_t;
void s3_fir_split_unaligned(const fir_job_t *);
typedef struct __attribute__((packed)) {
    uint32_t magic, sequence;
    uint64_t native_end, processed_pairs, resampled;
    uint32_t windows, candidates, gaps;
    uint16_t bin, coherence, run;
    uint8_t sf, type; /* 0 telemetry, 1 preamble candidate, 2 diagnostic window */
    uint32_t analysis_skipped;
    uint32_t crc;
} result_t;
_Static_assert(sizeof(result_t) == 60, "LRS1 size"); /* short USB packet: low latency */
static struct {
    unsigned sf, n, pos, hop, warm, phase, hpos, stable, norm;
    uint32_t input, target, out; /* local clocks; wrap is harmless modulo N */
    uint64_t total_out, seen, native_end, next_report;
    uint32_t windows, candidates, gaps, sequence, skipped, stale_skipped, last_generation;
    volatile uint32_t generation;
    volatile uint32_t job; /* 0 free, 1 core-0 work, 2 completed */
    uint32_t job_generation, job_start, work_pos, work_phase, work_stage, best, sum, best_bin;
    uint32_t last_window_start;
    uint16_t bin, quality;
    bool armed, diagnostic;
    volatile bool candidate_pending;
    int16_t *chirp, *history, *fft, *coeff, *hi, *hq;
    fir_job_t resample_job;
    int16_t resample_i, resample_q;
    lora_emit_fn emit;
} lr;
static void *alloc16(unsigned bytes) {
    return heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static float bessel(float x) {
    float sum = 1, t = 1;
    for (int k=1; k<25; k++) { float a=x/(2*k); t*=a*a; sum+=t; }
    return sum;
}
void lora_chirp_free(void) {
    free(lr.chirp); free(lr.coeff); free(lr.hi); free(lr.hq);
    memset(&lr, 0, sizeof(lr));
}
bool lora_chirp_setup(unsigned sf, bool diagnostic, lora_emit_fn emit, int16_t *fft_scratch, int16_t *history_scratch) {
    lora_chirp_free();
    if (sf<5 || sf>11 || !dsps_fft_w_table_sc16) return false;
    lr.sf=sf; lr.n=1u<<sf; lr.diagnostic=diagnostic; lr.emit=emit;
    lr.chirp=alloc16(4*lr.n); lr.history=history_scratch; lr.fft=fft_scratch;
    lr.coeff=alloc16(13*32*2); lr.hi=alloc16(72*2); lr.hq=alloc16(72*2);
    if (!lr.chirp || !lr.history || !lr.fft || !lr.coeff || !lr.hi || !lr.hq) {
        lora_chirp_free(); return false;
    }
    lr.resample_job=(fir_job_t){.groups=3,.nout=1,.shift=15,.oi=&lr.resample_i,.oq=&lr.resample_q};
    for (unsigned k=0; k<lr.n; k++) {
        double p=M_PI*((double)k*k/lr.n-k);
        lr.chirp[2*k]=(int16_t)lrint(32767*cos(p));
        lr.chirp[2*k+1]=(int16_t)lrint(-32767*sin(p)); /* conjugate upchirp */
    }
    for (unsigned ph=0; ph<13; ph++) {
        float h[24], sum=0;
        for (unsigned k=0; k<24; k++) {
            float m=k-11.5f-ph/13.0f, x=0.8125f*m, r=(k-11.5f)/11.5f;
            h[k]=0.8125f*(fabsf(x)<1e-6f?1:sinf(M_PI*x)/(M_PI*x))*
                 bessel(5.65f*sqrtf(fmaxf(0,1-r*r)))/bessel(5.65f);
            sum+=h[k];
        }
        for (unsigned k=0; k<24; k++) lr.coeff[ph*32+k]=(int16_t)lrintf(32767*h[k]/sum);
    }
    lr.next_report=25000;
    lora_chirp_gap();
    return true;
}
IRAM_ATTR void lora_chirp_gap(void) {
    if (lr.seen) lr.gaps++;
    lr.generation++;
    lr.input=lr.out=0; lr.target=31; lr.phase=lr.hpos=lr.pos=lr.hop=lr.warm=0;
    /* All 32 real samples must arrive before filtering: no zero-filled gaps. */
}
IRAM_ATTR static void report(unsigned type) {
    result_t r={.magic=0x3153524c, .sequence=lr.sequence++, .native_end=lr.native_end,
        .processed_pairs=lr.seen*64, .resampled=lr.total_out,
        .windows=lr.windows, .candidates=lr.candidates, .gaps=lr.gaps,
        .bin=lr.bin, .coherence=lr.quality, .run=lr.stable, .sf=lr.sf, .type=type,
        .analysis_skipped=lr.skipped+lr.stale_skipped};
    r.crc=esp_rom_crc32_le(0, (const uint8_t *)&r, 56);
    lr.emit(&r, sizeof(r));
}
IRAM_ATTR bool lora_chirp_work_slice(void) {
    if (lr.job!=1) return false;
    MEMW();
    if (lr.work_phase==0) {
      unsigned end=lr.work_pos+64; if (end>lr.n) end=lr.n;
      for (unsigned k=lr.work_pos; k<end; k++) {
        int32_t i=lr.fft[2*k], q=lr.fft[2*k+1];
        int32_t a=lr.chirp[2*k], b=lr.chirp[2*k+1];
        /* FIR output is bounded to ADC*32, leaving fixed-point headroom. */
        lr.fft[2*k]=(int16_t)((i*a-q*b+16384)>>15);
        lr.fft[2*k+1]=(int16_t)((i*b+q*a+16384)>>15);
      }
      lr.work_pos=end;
      if (end==lr.n) { lr.work_phase=1; lr.work_pos=0; }
      return true;
    }
    if (lr.work_phase==1) {
        s3_fft2r_sc16_rnd_stage(lr.fft,lr.n,dsps_fft_w_table_sc16,lr.work_stage++);
        if (lr.work_stage==lr.sf) lr.work_phase=2;
        return true;
    }
    unsigned end=lr.work_pos+128; if (end>lr.n) end=lr.n;
    for (unsigned k=lr.work_pos; k<end; k++) {
        int32_t i=lr.fft[2*k], q=lr.fft[2*k+1];
        uint32_t p=(uint32_t)(i*i+q*q); lr.sum+=p;
        if (p>lr.best) {
            lr.best=p;
            unsigned x=k, reversed=0;
            for (unsigned b=0; b<lr.sf; b++) { reversed=(reversed<<1)|(x&1); x>>=1; }
            lr.best_bin=reversed;
        }
    }
    lr.work_pos=end;
    if (end!=lr.n) return true;
    if (lr.job_generation!=lr.generation) { lr.stale_skipped++; goto done; }
    if (lr.last_generation!=lr.job_generation) {
        lr.stable=0; lr.armed=false; lr.last_generation=lr.job_generation;
    }
    else if (lr.job_start-lr.last_window_start!=lr.n/2) {
        /* A skipped analysis window cannot count toward a contiguous preamble. */
        lr.stable=0; lr.armed=false;
    }
    lr.last_window_start=lr.job_start;
    lr.windows++; lr.bin=lr.best_bin;
    /* Parseval bounds sum below 2*32767^2; avoid a flash libgcc 64-bit divide. */
    /* Exact 32-bit ratio for low powers; avoid amplitude-dependent scores
     * near the ADC/FFT quantization floor and 64-bit division on core 0. */
    lr.quality=!lr.sum?0:lr.sum<=4294967u?(lr.best*1000u)/lr.sum:lr.best/(lr.sum/1000u+1u);
    unsigned norm=(lr.best_bin-lr.job_start)&(lr.n-1);
    unsigned distance=(norm-lr.norm)&(lr.n-1);
    /* Compare the peak to eight times mean-bin power. A fixed energy
     * fraction would throw away LoRa's processing gain at higher SF. */
    if ((uint32_t)lr.quality*lr.n>=8000u) {
        if (lr.stable && (distance<=1 || distance>=lr.n-1)) { if (lr.stable<65535) lr.stable++; }
        else { lr.stable=1; lr.armed=false; }
        lr.norm=norm;
        if (lr.stable>=8 && !lr.armed) { lr.candidates++; lr.armed=true; MEMW(); lr.candidate_pending=true; }
    } else { lr.stable=0; lr.armed=false; }
done:
    MEMW(); lr.job=2;
    return true;
}
IRAM_ATTR static void window(void) {
    if (lr.job==1) { lr.skipped++; return; }
    MEMW();
    /* Copy a chronological window; the filter never waits for an FFT. */
    unsigned tail=lr.n-lr.pos;
    memcpy(lr.fft, lr.history+2*lr.pos, 4*tail);
    if (lr.pos) memcpy(lr.fft+2*tail,lr.history,4*lr.pos);
    lr.job_start=(uint32_t)(lr.out-lr.n); lr.job_generation=lr.generation;
    lr.work_phase=lr.work_pos=lr.work_stage=lr.best=lr.sum=0;
    MEMW(); lr.job=1;
    if (lr.diagnostic) {
        while (lora_chirp_work_slice()) {}
        if (lr.candidate_pending) { lr.candidate_pending=false; report(1); }
        report(2);
    }
}
IRAM_ATTR void lora_chirp_feed(int16_t i, int16_t q, uint64_t index) {
    lr.seen++; lr.native_end=(index+1)*64;
    lr.hi[lr.hpos]=lr.hi[lr.hpos+32]=i;
    lr.hq[lr.hpos]=lr.hq[lr.hpos+32]=q;
    lr.hpos=(lr.hpos+1)&31;
    if (lr.input++==lr.target) {
        /* Center 24 taps in the 32-sample history: same 15.5 input delay. */
        lr.resample_job.xi=lr.hi+lr.hpos+4; lr.resample_job.xq=lr.hq+lr.hpos+4;
        lr.resample_job.h=lr.coeff+32*lr.phase;
        s3_fir_split_unaligned(&lr.resample_job);
        lr.history[2*lr.pos]=lr.resample_i; lr.history[2*lr.pos+1]=lr.resample_q;
        lr.pos=(lr.pos+1)&(lr.n-1); lr.out++; lr.total_out++;
        if (lr.warm<lr.n) lr.warm++;
        if (++lr.hop>=lr.n/2) { lr.hop=0; if (lr.warm==lr.n) window(); }
        if (lr.phase>=10) { lr.phase-=10; lr.target+=2; }
        else { lr.phase+=3; lr.target++; }
    }
    if (!lr.diagnostic && lr.seen>=lr.next_report) { report(0); lr.next_report+=25000; }
    if (!lr.diagnostic && lr.candidate_pending) { lr.candidate_pending=false; report(1); }
}
void lora_chirp_finish(void) {
    while (lora_chirp_work_slice()) {}
    if (lr.candidate_pending) { lr.candidate_pending=false; report(1); }
    report(0);
}
