# Hardware test report — 7 October 2026

Updated through approximately 05:12 Hong Kong time. This is a measured engineering report, not a
claim of universal compatibility or a peer-reviewed paper. Subsequent research
results must be appended with their own firmware hashes and denominators.

## Main result

Latest public-receiver matrix: **239/240**, SF7, four coding rates and lengths
1–255 bytes; all forty 255-byte packets passed. Separate wider-band SF7
matrices passed **72/72 at 406.25 kHz** and **72/72 at 812.5 kHz**. The beginner
sketch passed **3/3** after a normal full PlatformIO upload. Later sections
identify each dataset and its firmware; the original matrix below is retained.
After the optional analog-control merge, its separate lowest-code regression
passed **72/72**, guards **18/18**, and default-gain bandwidth smoke tests
**3/3 each**. The newly rebuilt SendOnce example delivered **2/3**, with one
miss retained; the earlier 3/3 applies to the earlier image.

最新结论：公开接收器 239/240，覆盖 1–255 字节；两个更宽带宽批次各 72/72。
较早入门示例刷机后 3/3；最新重编译版本本轮 2/3，一次漏收保留。
模拟增益七档研究 70/70，最低档合入后回归 72/72；仍没有校准成 dBm。
SF8/SF9 的 PLL 路径有完整 CRC 包，但很不可靠；稳定高 SF、弱信号提高 SF
恢复和原生 Arduino 收包仍未实现，完整失败记录保留。

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
稳定 SF8/SF9、弱信号下提高 SF 的恢复效果、校准发射功率和原生 Arduino 完整接收，
在此快照中仍未具备，不能宣传为已完成。

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
ten trials, even when it reads 10/10.

The default SF7 up-chirp transmits about 59.5% of its samples. Down-chirp and
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

## Follow-up: amplitude versus spreading factor

Completed at approximately 03:55 HKT, with the same measured application image.
180 fresh 32-byte random packets, CR4/8, 10 randomized blocks, six DAC amplitudes
and three SF settings. The receiver was reconfigured to each transmitter SF.
No RF packet was retried. All 180 requested TX operations completed locally.

| DAC amplitude code | SF7 exact CRC | SF8 exact CRC | SF9 exact CRC |
|---:|---:|---:|---:|
| 1 | 0/10 | 0/10 | 0/10 |
| 3 | 1/10 | 0/10 | 0/10 |
| 10 | 10/10 | 0/10 | 0/10 |
| 30 | 9/10 | 0/10 | 0/10 |
| 75 | 10/10 | 0/10 | 0/10 |
| 150 | 10/10 | 0/10 | 0/10 |

![Amplitude and SF experiment](assets/amplitude-results.svg)

Receiver-reported median RSSI on successful SF7 packets increased from −86 dBm
at amplitude 3 (one survivor) to −62 dBm at amplitude 150. These are conditional
on successful reception; missing packets have no comparable RSSI value. The raw
amplitude is **not calibrated output power**, and small integer DAC codes also
reduce phase/amplitude resolution. This is not a calibrated attenuator sweep
and cannot isolate sensitivity from quantization effects. A 10/10 cell has a Wilson lower
95% bound of about 72.25%, so this small sample does not establish perfect delivery.

SF8/SF9 had zero CRC-valid requested receptions even at the largest amplitude.
This experiment does **not** demonstrate weak-signal recovery by increasing SF.
It reveals a waveform/backend limitation that must be resolved before such a
claim. The 40/180 aggregate mixes working and nonworking SF configurations and
must not replace the separate SF7 capability measurement.

Exact raw file: `evaluation/data/native-amplitude-sf.json` (original name
`native-matrix-power-1791316268459417400.json`). A fresh flash/rebuild regression
also repeated all 26 CR4/8 baseline payloads successfully, including Chinese;
its binary SHA256 matched the 240-case matrix exactly.

## Public receiver bring-up: retained failures

The public RadioLib 7.7.0 companion initially produced no delivered packet
lines with GPIO interrupts alone. Polling chip IRQ status produced the first
32-byte CRC-valid packet, but RX restart then returned SPI command error −706.
Entering standby before draining the FIFO/restarting RX fixed a three-packet
smoke test. These are implementation failures, not evidence that RadioLib or
the LR2021 hardware is incapable of receiving the waveform.

A subsequent randomized 240-request run received the first 16 requested
payloads correctly, but the sixteenth XIAO TX completion line timed out. The
host continued with one-line-shifted requests/replies, so later matching bytes
were attributed to the wrong trial. Only **15/240** satisfied the entire
recorded rule in this run. Its raw log is retained as
`evaluation/data/public-receiver-usb-desync.json`; it is a transport/test-run
failure and must not be pooled as an independent RF sensitivity estimate.

The sender example now flushes TXSTART, allows USB/tick service after RF, and
flushes TXEND. The verifier buffers complete lines, checkpoints each trial,
and aborts after any invalid local transaction rather than cascading stale
packets. The next dataset uses the normal complete PlatformIO upload workflow
on the verified XIAO, including bootloader and partition table.

Initial failed logs are included as `public-receiver-before-polling.json` and
`public-receiver-before-standby.json`. Do not overwrite failures with successful
retests or silently retry an RF packet.

## Public, reusable receiver: 239/240 after the fixes

Completed at approximately 04:17 HKT. The sender ran the repository's Arduino
SerialBench firmware, installed through **normal `pio run -e xiao-s3 -t upload`**
on the verified XIAO. The receiver ran the public RadioLib companion, with
hardware payload CRC presence explicitly required. No private AeroLink driver
or application is needed to reproduce this workflow.

The randomized matrix tested SF7, CR4/5–4/8 and lengths **1,8,32,80,128,255**,
10 fresh payloads per cell, seed 20261007. **239/240 (99.583%)** satisfied local
TX completion, fresh independently CRC-valid reception and full-byte equality.
All forty **255-byte** packets passed. The only miss was a one-byte CR4/8 case
(`0f`); there was no RX line for that trial. No reset or invalid TX transaction
was observed. This is still not perfect-delivery evidence.

The payloads, firmware and receiver implementation differ from the original
237/240 matrix. These are two separate engineering datasets, not a controlled
comparison of receiver quality or proof that an RF bug was fixed.

Raw file: `evaluation/data/public-receiver.json`.

![Separate hardware datasets, with binomial confidence intervals](assets/public-receiver-results.svg)

| Image | SHA256 |
|---|---|
| XIAO native SerialBench | `9a69a13204e841bf926cfb809f72c28f7a9718d192e70488b69b38a6a7d1ca70` |
| Public LR2021 companion | `2bd3fe2a98cf0927f6c7aff0f427fb8b54e76842b46aa634ee125a72ad8043a9` |

The latest companion also accepts an explicit `SF 7..9` setting for research.
That command was added after the 240-trial receiver image was built and must
be verified separately. The XIAO's PLL airtime guard, default DAC selection
and USB completion flushing are included in the sender image of this dataset.
Subsequent SF/BW commands were exercised by the SendOnce and bandwidth tests.

## Beginner example and wider bandwidths

The `SendOnce` Arduino sketch was installed with the normal complete PlatformIO
upload workflow and tested with three explicit `s` commands. **3/3** packets
were received with hardware CRC and the exact 16-byte `Hello from XIAO!`
payload. Raw file: `evaluation/data/sendonce.json`. Its image SHA256 is
`b90534f0701dda6f1c978f97f8c1d1fee904169f3d8ce49db2399337cea0ef73`.
This small smoke test proves that example worked on the bench, not reliability
across boards or environments.

A separate windowed-DAC research build extended bandwidth selection. SF7 at
**406.25 kHz: 72/72**, and **812.5 kHz: 72/72**, across all four coding rates
and lengths 1,8,32,80,128,255, three shuffled fresh-payload blocks per bandwidth.
The waveform windows were 7,500 and 3,750 samples respectively. A 72/72 sample
has a Wilson 95% lower bound of approximately 94.93%; each 3/3 cell has much
less precision. These are short bench measurements, not perfect delivery.
Raw files: `bandwidth-sf7-406-matrix.json`, `bandwidth-sf7-812-matrix.json`.

![Separate bandwidth measurements and calculated airtime](assets/bandwidth-results.svg)

The initial 406.25 kHz/SF7 test retained 15,000 samples, longer than its symbol,
and failed 0/3 with 108 late updates. Scaling the window fixed the measured
SF7 profile. The library now rejects windows at least as long as the symbol
before keying. Down-chirp and quarter-SFD windows use the actual symbol duration.
The 203.125 kHz/SF7 waveform values remain 15,000/6,000/1,000 samples.
After merging these changes, each of the three bandwidths passed another
3/3 fresh 32-byte CR4/8 packets on the actual library image. Its SHA256 is
`c19ddca83d71cf379804294d57f9124b98d0a94c7e6c85c2eb9a09d2582ede0d`.
The corresponding `merged-bandwidth-*-smoke.json` logs are included. Hardware
command/airtime/window guards also passed **14/14**; these are rejection tests,
not additional RF packet successes (`config-guards.json`).

SF8 at 406.25 kHz and SF9 at 812.5 kHz still failed **0/3 each**, despite their
59.5% up-chirp coverage and no late updates. Thus simply attributing every
higher-SF failure to low coverage would be unjustified. The remaining causes
include waveform/timing/header interoperability; no cause is proven yet.

## Continuous-stream research: failures retained

The upstream reader/writer experiment helped establish the 6 CPU cycles/sample
budget for 40 MS/s playback. Our internal word-ring version with calculated
down-chirps failed 0/3 at gap compensations 14 and 27. Removing periodic tick
service still failed 0/3. Unrolling down-chirp conjugation reduced the maximum
copy from about 4,112 to 2,505 cycles per 256-word block, still exceeding the
1,536-cycle reader budget, and again failed 0/3.

Precomputed up/down words in PSRAM failed SF7 0/3 with over 10,000 late blocks
per packet. SF8 aborted on its first trial with an interrupt-watchdog reset;
the remaining planned trials were not run. A nominal airtime cap cannot bound
an overrunning implementation. The isolated source now includes an actual
elapsed-cycle abort, added after that measured failure; it has not been promoted
to the main library. Logs and version hashes are in `evaluation/data/research/`.

The working library continues to use symbol windows. Continuous, gap-free
transmission, calibrated power and weak-signal SF recovery remain unachieved.

## Simple UI end-to-end proof

The final merged Arduino library was tested through the local simple website
with both English (16 bytes) and Chinese UTF-8 (48 bytes). Both arrived on the
independent HF LR2021, with hardware CRC and exact byte equality, and zero
reported late updates. Source/image metadata and both complete events are in
`evaluation/data/native-web-proof.json`. The UI's calculated waveform metadata
is descriptive; the actual PHY encoding and DAC playback ran on the XIAO.

![Actual browser result with complete received bytes](assets/live-proof.png)

The local controller explicitly restores frequency, bandwidth, CFO, preamble,
window and amplitude before sending, so a prior research setting cannot silently
carry into the default UI. The private AeroLink HF receiver application was
restored from its verified backup for this web demonstration; the public
RadioLib companion is the separately tested reproduction path.

## Clean cloud compilation

[GitHub Actions run 37528675081](https://github.com/jimmywuhkust/esp32-lora-sdr/actions/runs/37528675081)
completed successfully for source commit `1b36aed3ed86b10edd5e96a9d44851019868eb01`
on a fresh Ubuntu runner. It built SerialBench, SendOnce and the public LR2021
companion using pinned PlatformIO dependencies. Total duration was 1 minute
57 seconds. This is additional clean-build evidence; the runner has no radio
hardware and its success is not an RF test.

## Analog gain control with constant waveform quantization

An isolated build held DAC amplitude at 150 and varied both PBUS (5,1) and
(5,3), inspired by the credited upstream IQ TX study. **70/70** fresh 32-byte
SF7/CR4/8 packets passed: seven codes 1,3,8,16,32,48,63, ten shuffled blocks,
no RF retries. Every trial also verified both gain readbacks and successful
local completion. Raw file: `evaluation/data/analog-gain-sf7-sweep.json`.
The image SHA256 was
`884a7ac0def5183d2e7b47caa46ee3e88e054e136204ae7e68ff05105a58138f`.
Each 10/10 point has a Wilson 95% lower bound of only about 72.25%.

Median received RSSI was -91.5,-85.5,-88.5,-91.5,-87.5,-85.5,-62.5 dBm
respectively. Thus signal level changed substantially without lowering DAC
quantization, but **the codes are not monotonic and are not calibrated power**.
There was no receive threshold crossing or SF recovery in this experiment.
These results do not establish sensitivity, range or a legal spectral mask.

![Constant-DAC analog gain study with confidence intervals](assets/analog-gain-results.svg)

The reference's keyed default 119 did not match this build's measured 63/63.
A non-raising guard rejected 119 before chirp playback. The first 63 smoke
run then aborted after a fragmented USB completion line; its one exact and
one corrupt RF reception are retained, not discarded or pooled with the
completed sweep. Shortening successful completion lines below 64 bytes gave
a fresh 3/3 smoke pass. All initial failure logs are in `evaluation/data/research/`.
This remains an isolated research feature, not a conventional dBm API.

## Further higher-SF diagnosis

Increasing the down-chirp and quarter-SFD windows in the separate bandwidth
build still gave SF8/406.25 kHz **0/3** and SF9/812.5 kHz **0/3**, with no
late updates. The failed variants remain separate from the supported library.

A different native PLL transport gave SF8/203.125 kHz **0/3**. SF9 at the
same bandwidth received **1/3**, then **2/10** in a longer fresh-payload run
at 80,000 register updates/s. The first three payloads were repeated between
those two runs, so they must not be pooled as independent trials. The SF9
exact CRC packets show some higher-SF encoding interoperability, while the
many CRC failures show the PLL waveform is not reliable. They do not validate
SF9 DAC transmission or weak-signal SF recovery. Raw local completion and
all received corrupt bytes are retained in `evaluation/data/research/`.

Increasing the PLL update rate gave SF9 **4/10** at 120,000 updates/s and
SF8 **1/10** at 200,000 updates/s, with the same fresh-payload sequence per
run. These were small sequential diagnostic trials, not a randomized rate
comparison; their low delivery remains unsuitable for a supported profile.

The lowest analog code was subsequently exercised across four coding rates
and six lengths (1,8,32,80,128,255), three shuffled blocks. The separate
research build delivered **71/72**, including all twelve 255-byte packets.
One 32-byte CR4/8 trial had no exact CRC-valid reception; it was retained and
not retried. This matrix does not show perfect delivery or a receive threshold.
Raw file: `evaluation/data/analog-gain1-matrix.json`.

## Optional analog-control library regression

The library now exposes an explicitly experimental `analogGainCode`. Default
0 skips PBUS changes. Nonzero codes 1–63 are DAC-only, checked against both
actual keyed defaults, verified after programming, and restored with readback
before returning. A restoration failure returns a local error.

The merged SerialBench image SHA256 is
`459905f6993f9f5062c54eb4e3e9ada28384ad6f60dd00a66e6ce8b1532dee39`.
A separate code-1 regression delivered **72/72** across all four coding rates
and six lengths, including twelve 255-byte packets. It reused the research
matrix's payload sequence; this is a firmware regression, not additional
independent randomized data to pool with 71/72. Raw file:
`evaluation/data/merged-analog-gain1-matrix.json`.

All **18/18** command/airtime/window/transport guards passed, including
out-of-range analog requests and rejecting analog control on PLL before RF.
The default `PA 0` path then delivered **3/3** fresh 32-byte packets at each
of the three SF7 bandwidths. These small smoke tests preserve the earlier
full-matrix denominators rather than claiming another full default matrix.

The rebuilt SendOnce sketch was installed with the full normal PlatformIO
upload and delivered **2/3** complete 16-byte CRC packets. Its first requested
packet produced no RX line despite successful local completion and zero late
updates; the cause is unproven. There was no retry to replace it. The new
image SHA256 is
`38c6c1ba8721006241883f852e7665c6ee629cd831dd8f01fa2c90c361b3acf5`.
Raw file: `evaluation/data/analog-sendonce-smoke.json`. The earlier 3/3 test
used another image; it does not make this missed packet disappear.
