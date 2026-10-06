/* SPDX-License-Identifier: GPL-3.0-or-later
 * Experimental SX1280 narrow-band chirp demodulator, not a packet decoder. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef bool (*lora_emit_fn)(const void *, unsigned);
bool lora_chirp_setup(unsigned sf, bool diagnostic, lora_emit_fn emit, int16_t *fft_scratch, int16_t *history_scratch);
void lora_chirp_free(void);
void lora_chirp_gap(void);
/* Positive-frequency RF I/Q, 250000 complex samples/s, signed ADC * 32. */
void lora_chirp_feed(int16_t i, int16_t q, uint64_t index);
void lora_chirp_finish(void);
/* Core 0: bounded dechirp / one FFT stage / peak-search slices. */
bool lora_chirp_work_slice(void);
