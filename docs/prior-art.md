# Prior art and contribution

Research snapshot: 7 October 2026. No world-first claim is made.

| Work | Mechanism | Evidence relevant to this project |
|---|---|---|
| [LoLRa, CNLohr](https://github.com/cnlohr/lolra) | MCU GPIO/APLL harmonics and synthesized bit streams | Firmware-generated LoRa received by commercial radios predates this project. Its ESP32-S2 mechanism differs from the S3 internal 2.4 GHz RF chain. |
| [ESP-SDR, ESPARGOS](https://github.com/ESPARGOS/esp-sdr) | Undocumented ESP32 I/Q capture | Foundation for our S3 capture firmware; upstream describes low-duty-cycle reception. GPLv3 attribution and license are retained. |
| [esp32-sdr-trx, Jochen Hammes](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/docs/research/LORA-IQ.md) | S3 RF PLL polar modulation and SRAM DAC I/Q playback | Documents LoRa TX and RX using SDR/software decoders. The report explicitly leaves real LoRa-chip reception untested. Narrowband and truncated-symbol limitations are valuable prior evidence. |
| [Wi-Lo, Gawlowicz et al., 2021](https://arxiv.org/abs/2105.04998) | 802.11b CCK waveform emulation | Atheros AR928x WiFi-to-SX1280 packet experiments predate this work. Mandatory WiFi preambles and interframe spacing distort longer emulated frames. It is not an ESP32-S3 SRAM playback implementation. |
| [WiRa, Xia et al., INFOCOM 2022](https://xiaolongbupt.github.io/homepage_files/%5BPaper%5DWiRa_INFOCOM2022.pdf) | 802.11ax resource-unit emulation and frame aggregation | Prototype uses USRP N210 and SX1280. Cyclic prefixes and subframe headers cause errors; the design introduces mode flipping and symbol handling. A USRP implementation is not a downloadable S3 library. |
| [WiLo, Gao et al., IEEE TCOM 2025](https://scholars.cityu.edu.hk/en/publications/wilo-long-range-cross-technology-communication-from-wi-fi-to-lora/) | PHY cross-technology transmission | The institutional abstract reports USRP and commodity-device experiments. This similarly named work is distinct from the 2021 Wi-Lo. Abstract/bibliographic evidence was reviewed; no claim of reproducing its implementation is made. |
| [Wi-Lo++, Rösler et al., IEEE IoT Journal, online first July 2026](https://www.tkn.tu-berlin.de/bib/roesler2026wi-lo/) | Consecutive CCK frames, distortion placement and overlapping interfaces | The authors' full paper describes modified NIC firmware for tight timing. Its released full framework is SF<7; higher-SF user-level simulation/packet-calculation code is separately described. This is especially relevant to continuity limits. |
| [XFi, Liu et al., ICNP 2020](https://webarchive.ucr.edu/icnp20.cs.ucr.edu/proceedings/main/XFi.pdf) | Reconstruct IoT waveforms from decoded, collided WiFi payloads | RTL8812au/SX1280 reception precedes this project. It requires an overlapping WiFi transmission and access to corrupt WiFi payloads; it is not ordinary standalone LoRa demodulation by a stock WiFi packet receiver. |

The contribution we are evaluating is reproducible ESP32-S3-to-LR2021 packet
interoperability, a usable library, beginner tooling and a measured capability
matrix. This is a contribution statement, not a novelty proof or publication claim.

WiFi-to-commercial-LoRa communication therefore predates this project in both
papers and open hardware/software work. Our narrower S3-to-LR2021 bench result
must not be marketed as inventing WiFi-to-LoRa communication. No prior work's
range or reception percentage is a measurement of this XIAO.

## Learning from failed experiments

[The upstream DAC investigation](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/docs/research/IQ-TX-PHASE-A.md)
helped us correct three register assumptions: bit19 holds playback, the count
field is length-minus-one, and the tone generator must be disabled without
unkeying the RF chain. We preserve unsuccessful attempts in the raw bench data.

That branch is licensed [0BSD](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/LICENSE).
LoLRa has mixed per-file licenses; we have not bundled its RF implementations.
External measurement numbers are prior-art reports, not measurements of our board.

The Wi-Lo and WiRa full papers reinforce a practical lesson: a recognizable
chirp spectrum is not sufficient. Immutable headers, mandatory gaps and
discontinuities can damage synchronization and decoded symbols. Wi-Lo++
explicitly studies moving distortion away from critical regions and preserving
timing across packet boundaries. Those mechanisms motivate our independent
CRC tests and retained continuous-stream failures; they are not a proven fix
for our current DAC implementation. The experiments here directly access RF
SRAM rather than synthesizing normal CCK/OFDM packet payloads.

## What the published failures tell us

The upstream LoRa investigation reports that a truncated chirp below roughly
half a symbol broadened the dechirped peak and broke classes of symbol values.
It also reports that attempted sync-based offset calibration worsened some
captures and that its gated DAC waveform missed the intended SNR target.
Those are particularly relevant to our SF8/SF9 failures: increasing SF doubles
the symbol while our RF SRAM window remains bounded. This is a plausible
mechanism, **not an established diagnosis** of our hardware failures.

Its [E1/E2 investigation](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/docs/research/DESIGN-IQ-TX.md)
reports corrupted SRAM when either CPU or GDMA writes the bank being played.
Our first cursor exceeded the copying budget. A later LUT assembly writer met
that budget but still failed CRC reception. Paired digital readbacks on our
XIAO then gave zero wrong words while idle and 16,201/16,384 while playing,
in each of three pairs. This reproduces a failure of playing-bank data integrity
for our writer; it is not proof of the exact RF distortion or silicon cause.
The corruption fraction differs from the upstream's different pattern/writer.
Research stays excluded from the working windowed-DAC library. The upstream
open question about revoking the playing-bank grant subsequently motivated
our grant-release experiment: zero readback errors in three deterministic
triplets and a separate48/48 bounded SF7 RF matrix. SF8 still failed. These
new measurements are ours, not an upstream result; see the
[grant experiment](../research/playing-bank-grant/README.md).

LoRa SDR receiver research also predates this work: Tapparel et al.,
[An Open-Source LoRa Physical Layer Prototype on GNU Radio (2020)](https://arxiv.org/abs/2002.08208),
reports a compatible open PHY and timing/frequency compensation; Ghanaatian
et al., [LoRa Digital Receiver Analysis and Implementation (2019)](https://arxiv.org/abs/1811.04146),
analyzes receiver algorithms and carrier/sample-frequency offsets. These
papers motivate measuring synchronization and packet error rates separately
from encoder agreement; we have not reproduced their numerical results.

This focused review is not a systematic exhaustive search or evidence that no
other ESP32-S3-to-commercial-chip demonstration exists. The repository credits
known prior work and makes no first-of-its-kind claim.


For the higher-SF follow-up we also checked the original
[GNU Radio modulator](https://github.com/tapparelj/gr-lora_sdr/blob/master/lib/modulate_impl.cc)
and [LoRaPHY source](https://github.com/jkadbear/LoRaPHY/blob/master/LoRaPHY.m).
The sync-word nibble-to-symbol shift is fixed at three bits rather than being
scaled with SF. These are algorithm references, not evidence that our emitted
RF waveform matches them.
