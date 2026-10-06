# Hardware test report — 7 October 2026

Snapshot: 03:35 Hong Kong time. This is a measured engineering report, not a
claim of universal compatibility or a peer-reviewed paper. Subsequent research
results must be appended with their own firmware hashes and denominators.

## Main result

**A XIAO ESP32-S3 genuinely transmitted complete 2.4 GHz LoRa packets to an
independent LR2021, with matching full payload and hardware CRC.** The portable
encoder and RF modulation ran on the XIAO in the native Arduino build.

The completed randomized SF7 matrix achieved **237/240 packets (98.75%)**.
Its descriptive Wilson 95% interval is **96.39–99.57%**. All four coding rates
and lengths 1, 8, 32, 80, 128 and 250 bytes were tested, ten fresh payloads per
combination. No successful retransmission was substituted for a failed trial.
There were three misses; this result does not justify a 100% reliability claim.

中文结论：XIAO 已经能用自带射频发送完整 LoRa 包，LR2021 独立接收并通过 CRC。
完整随机实测为 237/240，覆盖四种纠错率和 1–250 字节；三个失败保留。
SF8/SF9、弱信号下提高 SF 的恢复效果、校准发射功率和原生 Arduino 完整接收，
在此快照中仍未验证成功，不能宣传为已经具备。

![Real RF results and confidence intervals](assets/baseline-results.svg)

[PDF figure](assets/baseline-results.pdf) · [PNG figure](assets/baseline-results.png) ·
[Plot source](../evaluation/plot_results.py) · [Raw matrix](../evaluation/data/native-matrix-basic.json)

## Test setup and success rule

| Item | Setup |
|---|---|
| Sender | Seeed XIAO ESP32-S3, revision 0.2, 40 MHz crystal, 8 MB flash / 8 MB OPI PSRAM |
| Receiver | Separate AeroLink ESP32-S3 + LR2021, HF port, HF lab firmware 2.9.18-hf2 |
| RF path | Connected antennas, stationary indoor bench, over the air |
| Channel / bandwidth | 2440.125 MHz / 203.125 kHz |
| Main PHY | SF7, 16-symbol preamble, sync0x12, explicit header, payload CRC required |
| Modulation | 40 MS/s RF SRAM DAC windows, amplitude150, up-window15000 samples |
| LO correction | +15 kHz, measured for this pair, not a universal calibration |
| Build | PlatformIO espressif32 7.0.1; Arduino core 2.0.17; XIAO S3 board profile |
| Matrix order | 10 randomized blocks, one of each parameter combination per block, seed20261007 |
| RF retries | None |
| Independent criterion | Fresh LR2021 RX event, CRC valid, full hex exactly equal to requested payload |

Spectrum energy, a detected preamble, a local TX-end line or a software round
trip alone never counts as successful reception. The receiver does not receive
the expected bytes as a decoding hint. Old serial events are discarded before
each transaction. Safe retries of idempotent settings are recorded separately
and do not retransmit RF.

## Completed matrix

Each cell reports successful packets / transmitted packets.

| Coding rate | 1 byte | 8 bytes | 32 bytes | 80 bytes | 128 bytes | 250 bytes |
|---|---:|---:|---:|---:|---:|---:|
| 4/5 | 9/10 | 10/10 | 10/10 | 10/10 | 10/10 | 10/10 |
| 4/6 | 10/10 | 10/10 | 10/10 | 10/10 | 9/10 | 10/10 |
| 4/7 | 10/10 | 10/10 | 10/10 | 10/10 | 10/10 | 10/10 |
| 4/8 | 10/10 | 10/10 | 10/10 | 10/10 | 9/10 | 10/10 |

LR2021 aggregate counters changed by +237 CRC-validated packets and +2 CRC
errors, with no missing-CRC acceptances, invalid metadata or host drops.
These aggregate counters do not identify the exact mechanism of every miss.
Successful packet RSSI was approximately −62 dBm on this bench; that is the
receiver's reported signal level, not calibrated XIAO output power.

The complete-frame airtimes in panel C come from the encoder, including
preamble, synchronization, header, FEC and payload. They are calculated PHY
durations; they are not a measured RF duty cycle or application throughput.

## Same-payload regression and coding checks

| Experiment | Result | Interpretation |
|---|---:|---|
| Host PLL, SF7, CR4/8 | 13/26 | Complete packet successes exist, but general payload reliability was poor |
| Host DAC windows, SF7, CR4/8 | 26/26 | Corrected DAC configuration, real independent LR2021 reception |
| Native Arduino DAC before SFD timing fix | 25/26 | Chinese 30-byte payload missed; one late symbol boundary |
| Native Arduino DAC after SFD pre-staging | 26/26 | Same payloads, including the Chinese packet, zero late boundaries |
| Native encoder cross-check | 264/264 | SF7–12 × CR1–4 × eleven lengths; **no RF was emitted by this check** |

The 26-payload set contains three random binary payloads at lengths 1, 2, 3,
8, 17, 32, 64 and 80, plus English16 and Chinese30. The method comparison uses
that same set. Firmware and uncalibrated RF levels changed, so these bars are
development regressions, not a controlled sensitivity comparison.

Encoder lengths are 1, 2, 3, 8, 17, 32, 64, 80, 128, 250 and 255 bytes.
The independent pinned reference omits a one-byte CRC special case; that case
was corrected using the separately pinned lora-phy CRC convention and recorded
in the cross-check JSON. Matching another implementation does not prove every
packet mode matches a commercial radio.

## Failures that changed the implementation

1. **Wrong DAC control assumptions.** Bit19 holds playback; it does not repeat
   it. The count is length-minus-one. Disabling the tone must preserve the keyed
   RF chain. These facts were learned from the earlier 0BSD investigation and
   then checked on this board. The failed probes were not LoRa interop successes.
2. **Arduino SDK initialization.** Stopping/deinitializing Wi-Fi left playback
   without DONE despite additional clock enables. Minimal NULL-mode driver
   initialization without association fixed the actual engine and RF behavior.
3. **Quarter-SFD deadline.** Preparing the first header window only after the
   quarter SFD caused one late boundary. Pre-staging its tail before SFD reduced
   the post-SFD copy and removed that late update; 25/26 became 26/26 on the
   shared baseline set.
4. **Long-packet watchdog reset.** An earlier 250-byte CR4/7 trial triggered the
   interrupt watchdog while the entire packet masked interrupts. The new path
   services pending ticks in short idle gaps. The completed 240-case matrix had
   no such reset, including all forty 250-byte trials.
5. **PSRAM bandwidth.** SF8 full-ring reads from PSRAM took about1.39ms per copy
   and missed symbol deadlines. Compact phase tables in internal RAM fixed
   timing, but SF8/SF9 reception still failed in these windowed trials. Valid
   timing counters are not packet proof.
6. **Host recording lock.** One earlier matrix was interrupted by a OneDrive
   file-write error. The final matrix used immutable per-trial checkpoints.
   Partial runs remain separate and are not merged into the completed denominator.

One pre-fix run contained an unrelated short CRC-valid packet with a different
payload. It was retained in the raw log and did not count as a requested-packet
success. Short payloads alone are therefore weaker demonstration evidence than
fresh long random payloads with exact-byte verification.

## Limits of the result

This is one board pair in one stationary indoor setup, without calibrated RF
power, a shielded box, an attenuator sweep or an independent spectrum analyzer.
No distance, sensitivity or emission-compliance number is established. Wilson
intervals describe a Bernoulli model of these trials; correlated interference
and a single bench limit generalization. Each individual matrix cell has only
ten trials, even when it reads10/10.

The default SF7 up-chirp transmits about59.5% of its samples. Down-chirp and
quarter-SFD windows are shorter. There are deliberate silent gaps. Raising SF
lengthens symbols while the playback window is bounded, reducing transmitted
energy fraction; a higher-SF range benefit cannot simply be assumed.

SF5/SF6 RF, other bandwidths, frequency sweeps, implicit headers, CRC-off RF,
SF10–12 RF and 251–255-byte reception remain unverified. Native Arduino packet
RX returns `Unsupported`. The earlier reverse link uses separate XIAO I/Q
capture firmware and a PC decoder, with gaps between capture windows.

## Reproduce and identify this dataset

Primary raw file: `native-matrix-basic-1791315033749861300.json`, copied without
editing into `evaluation/data/native-matrix-basic.json`. Application image
SHA256:

```text
34afeca78440f016ee099ef337c5277d23d37d2b96c4aa75655ce76eab2650b6
```

Firmware/source hashes, exact payloads, reception records, timestamps, seed,
transport status and diagnostic counters are embedded in the raw file.
The working image was preserved as `native-dac-watchdog.bin` in the local
hardware workspace. Source snapshots were preserved alongside local test logs.
Later research changes must not be described as this exact measured build.

To regenerate the figures from the included dataset:

```sh
python -m pip install matplotlib==3.11.2 numpy==2.5.3
python evaluation/plot_results.py
```

## Prior art

We do not claim to be first. [LoLRa](https://github.com/cnlohr/lolra) predates this
work. [ESPARGOS esp-sdr](https://github.com/ESPARGOS/esp-sdr) supplies the capture
foundation. [Jochen Hammes' LoRa report](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/docs/research/LORA-IQ.md)
documents S3 LoRa through software receivers and explicitly leaves real-chip
reception untested. [Its failed DAC experiments](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/docs/research/IQ-TX-PHASE-A.md)
directly informed our debugging. Our useful evidence here is measured LR2021
interop, native library execution and retained successes **and** failures.
