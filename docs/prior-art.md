# Prior art and contribution

Research snapshot: 7 October 2026. No world-first claim is made.

| Work | Mechanism | Evidence relevant to this project |
|---|---|---|
| [LoLRa, CNLohr](https://github.com/cnlohr/lolra) | MCU GPIO/APLL harmonics and synthesized bit streams | Firmware-generated LoRa received by commercial radios predates this project. Its ESP32-S2 mechanism differs from the S3 internal 2.4 GHz RF chain. |
| [ESP-SDR, ESPARGOS](https://github.com/ESPARGOS/esp-sdr) | Undocumented ESP32 I/Q capture | Foundation for our S3 capture firmware; upstream describes low-duty-cycle reception. GPLv3 attribution and license are retained. |
| [esp32-sdr-trx, Jochen Hammes](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/docs/research/LORA-IQ.md) | S3 RF PLL polar modulation and SRAM DAC I/Q playback | Documents LoRa TX and RX using SDR/software decoders. The report explicitly leaves real LoRa-chip reception untested. Narrowband and truncated-symbol limitations are valuable prior evidence. |

The contribution we are evaluating is reproducible ESP32-S3-to-LR2021 packet
interoperability, a usable library, beginner tooling and a measured capability
matrix. This is a contribution statement, not a novelty proof or publication claim.

## Learning from failed experiments

[The upstream DAC investigation](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/docs/research/IQ-TX-PHASE-A.md)
helped us correct three register assumptions: bit19 holds playback, the count
field is length-minus-one, and the tone generator must be disabled without
unkeying the RF chain. We preserve unsuccessful attempts in the raw bench data.

That branch is licensed [0BSD](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/LICENSE).
LoLRa has mixed per-file licenses; we have not bundled its RF implementations.
External measurement numbers are prior-art reports, not measurements of our board.
