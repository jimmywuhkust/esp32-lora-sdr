#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    int16_t* iq;
    unsigned samples, capture_status, drops, abandoned;
    uint64_t first_sample;
    uint64_t capture_us;
} lora_native_capture_t;
int lora_sdr_platform_begin(void);
int lora_sdr_platform_capture(uint32_t frequency,unsigned window_ms,lora_native_capture_t*);
bool native_frames_to_iq(const uint8_t*,unsigned,int16_t**,unsigned*,uint64_t*);
#ifdef __cplusplus
}
#endif
