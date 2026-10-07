#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool native_decode_iq(const int16_t*,unsigned samples,unsigned sf,unsigned sync,uint64_t first);
bool native_decode_frames(const uint8_t*,unsigned bytes,unsigned sf,unsigned sync);
bool native_transmit_hex(unsigned sf,unsigned cr,const char* hex);
void native_receive_command(unsigned sf,unsigned window_ms);
void native_capture_ready(void);
bool native_setting_command(const char* command);
#ifdef __cplusplus
}
#endif
