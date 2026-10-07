# Arduino on-device LoRa: three-radio verification

7 October 2026, Hong Kong time. [中文](arduino-report.zh-CN.md) ·
[Build the application](arduino-rx.md) · [API](api.md)

Arduino reception is implemented: `setup()` / `loop()` applications capture
IQ, synchronize, demodulate, decode FEC and check the complete payload CRC
on the ESP32-S3. The same `LoRaRadio` object transmits complete packets.
The supplied PlatformIO profile builds Arduino 2.0.17 as an IDF 4.4.7
component. A stock Arduino ZIP installation still supplies TX only.

## What this experiment establishes

Two ESP32-S3 boards exchange actual LoRa packets using their internal RF
chains. Both have independently received from and transmitted to an LR2021.
Autonomous ping/pong also succeeds in both role assignments. The host never
encodes IQ or decodes RF in these tests. In the autonomous pair sessions it
writes **zero serial bytes to either ESP32**; USB only powers and logs them.

Reception remains experimental. Finite capture/decode windows leave blind
time, packets are missed, and a CRC-valid console packet with incorrect
payload bytes was observed in an intermediate session. CRC checks alone
are insufficient evidence of byte correctness. All reported successes
require complete independently expected payload bytes as well as CRC.
Applications needing stronger integrity should add an application checksum
or authenticated framing; CRC is not authentication.

## Hardware and fixed RF profile

| Device | Identified hardware | Role |
|---|---|---|
| A / COM3 | Seeed XIAO ESP32-S3, rev0.2, 40 MHz, 8 MB flash + 8 MB OPI PSRAM | Internal-RF TX/RX |
| B / COM5 | ESP32-S3 rev0.2, 40 MHz, embedded 8 MB OPI PSRAM + 8 MB flash; board model unverified | Internal-RF TX/RX |
| LR2021 / COM4 | AeroLink S3 + LR2021, HF 2.4 GHz path, public RadioLib 7.7.0 companion | Independent TX/RX |

Frequency 2440.125 MHz, bandwidth 203.125 kHz, preamble 16, sync `0x12`,
standard IQ, explicit header and payload CRC. ESP32 TX amplitude is 75%
(DAC peak 150), with +15 kHz configured correction. This is relative
amplitude, **not calibrated dBm**. LR2021 requests −12 dBm. Antenna distance,
radiated power, attenuation and background RF were not measured. These are
small stationary indoor bench samples, not sensitivity, range or reliability
measurements. A board identity is not a verified pinout for every S3 module.

## Acceptance and denominators

The three-radio fixture makes one fresh RF transmission per case, with no
RF retries. It compares all returned bytes against independently generated
payloads **after** MCU decoding. ESP32 acceptance requires a complete packet,
explicit-header checksum, matched sync and payload CRC. The LR2021 gate
requires RX_DONE, HEADER_VALID and CRC_OK IRQs (bits 4, 5, 18), rejects
header/payload CRC error IRQs (bits 9, 22), and requires status zero, correct
length and exact payload. Mixed error IRQs remain failures.

Autonomous requests are 20 bytes: `PING`, a random session, sequence and
eight random bytes. The responder replaces `PING` with `PONG` after MCU CRC
acceptance. The initiator requires the complete matching reply. Each unique
request has eight explicitly logged RF copies. A decoded request triggers
eight reply copies. Those copies are not independent unique successes.
The example waits 6.5 s before replies, listens in 500 ms windows, and stops
after four unique requests. See [the timing guide](arduino-rx.md#two-esp32s).

The host observer also logs LR2021 reception of requests and replies. It
does not supply expected bytes to either ESP32's decoder. Bench scheduling,
autonomous behavior, saved-IQ replay and software codec tests are separate
forms of evidence and are never pooled into one delivery rate.

## Final three-radio results

The stack-corrected image completed all 24 fresh cases, seed
`202610071709`. Each direction used SF7, CR4/5 and CR4/8, and 8- and
32-byte payloads, with one transmission for each combination.

| Directed link | Full CRC + exact payload |
|---|---|
| A → LR2021 | 4/4 |
| B → LR2021 | 4/4 |
| LR2021 → A | 4/4 |
| LR2021 → B | 4/4 |
| A → B | 3/4 |
| B → A | 3/4 |

Total **22/24**, including **16/16** LR2021-directed cases and **6/8** peer
cases. A→B CR4/5 32 bytes and B→A CR4/5 8 bytes failed full CRC acceptance.
All four CR4/8 peer cases passed. All 16 receive captures reported status
zero, zero drops and zero abandoned units. Three accepted packets used soft
FEC. Failed peer decode times were 2.74 s and 2.32 s: acquisition continuity
does not eliminate decoding or blind-time limitations.
[Raw trials](../evaluation/data/arduino-three-radios-final.json).

The separate transmitter matrix, seed `202610071720`, delivered **18/20**
strict CRC-valid exact packets over CR4/5–4/8 × lengths 1, 8, 32, 80, 255.
All four 255-byte cases passed. CR4/5 80 bytes and CR4/6 80 bytes failed the
strict receiver gate. [All attempts](../evaluation/data/arduino-tx-final.json).

A separate RX smoke test, seed `202610071721`, received **6/6** fresh 8-byte
LR2021 packets, one at each SF7–12. SF7 used CR4/8; other SFs used CR4/5.
SF10–12 used 900 ms captures. SF12 took **18.93 s** to decode. Four negative
cases were correctly rejected: no RF, no payload CRC, wrong sync and a
truncated packet window. All ten captures had zero status/drops/abandoned.
These six short positives do not validate long/high-SF payloads or sensitivity.
[Full smoke test](../evaluation/data/arduino-rx-smoke-final.json).

The standalone Arduino echo application, seed `202610071727`, received
**8/8** fresh SF7 packets and returned **8/8** exact `ACK:` packets accepted
by the independent strict LR2021 gate. Inputs span CR4/5–4/8 and 8/32 bytes.
The fixture writes zero bytes to the ESP32. It explicitly rearms LR2021 RX
after its own TX. [Both complete console streams](../evaluation/data/arduino-echo-final.json).

The final autonomous **A initiates → B** session completed **3/4** full
CRC-valid, byte-exact round trips. B decoded all four correct requests;
LR2021 independently accepted all four unique requests and all four unique
replies. A missed the first reply train. The observer logged 32 request and
32 reply transmissions, all 64 with zero late segments. Actual DAC windows
were 13,500–13,600 samples. Successful replies used hard, soft and CRC-aided
soft paths; expected bytes were checked only in the initiator's application.
[Unedited session](../evaluation/data/arduino-ping-pong-final-ab.json).

The final **B initiates → A** session also completed **3/4** full round trips.
A decoded all four correct requests; LR2021 accepted every unique request
and reply. B missed the fourth reply train. All 64 observed transmissions
(32 requests, 32 replies) had zero late segments.
[Unedited reverse-role session](../evaluation/data/arduino-ping-pong-final-ba.json).
The eight responder captures across these two sessions had zero capture
status, drops and abandoned units. Both sessions used zero ESP32 serial writes.

![Measured RF links and autonomous pair sessions](assets/arduino-duplex.svg)

The earlier/final bars use separate payload sets and firmware. This is a
descriptive engineering comparison, not a controlled ablation or statistical
reliability estimate. Recreate SVG, PNG and PDF with
`python evaluation/plot_arduino.py`; every plotted numerator and denominator
comes from the included raw JSON. [Exportable PDF](assets/arduino-duplex.pdf).

## Why standalone Arduino transmission initially failed

The original fixed 15,000-sample DAC playback window left about 255 μs to
copy the next chirp. Actual Arduino copy time reached 72,324 CPU cycles,
about 301 μs at 240 MHz. One observed packet had **81 of 85 late segments**.
All four unique requests failed in that retained session.

Reacquiring the DAC clock did not solve it: before/after clock registers were
both `ffffdfdf`, and another four-request session still had 81 late segments
per transmission. Interrupt masking alone did not establish compatibility.

The backend now measures three SRAM copies before native SF7 TX. It chooses
a played window that leaves the measured copy time plus 8,192 CPU cycles
(about 34 μs) of margin, rounded down to 100 samples. The LoRa symbol period
stays fixed. The measured pair typically used 13,500–13,600 active samples
instead of 15,000, with zero observed late segments in successful adaptive
sessions. The waveform still has gaps; this is not continuous DAC streaming.

The reference-offset benchmark subsequently exposed an 8 KiB `loopTask`
stack overflow. Its incomplete run is retained, including the reboot log.
Moving the 2.2 KiB symbol workspace to checked dynamic allocation reduces
decoder stack use; final measurements identify the rebuilt images separately.

## Decoder changes and integrity limits

Capture stores signed 16-bit FIR output at 250 kcomplex samples/s in OPI
PSRAM. The physical ADC remains 10-bit. A coarse unpadded FFT scans preambles;
the padded FFT refines candidates. Five bounded timing/folding hypotheses
and fractional reference offsets address distorted, gated chirp peaks.
Every accepted hypothesis still requires the full packet CRC.

SF7 can fall back to FFT bit confidence and weighted valid Hamming codewords.
If needed, a bounded list tries second-best words at up to six weak payload
positions: at most 63 additional candidates. Received CRC and the last two
payload bytes stay fixed; no expected bytes or solved checksum are inserted.
Only one complete CRC-valid list candidate is accepted. `softDecoded` and
`crcAided` expose these paths. More hypotheses increase undetected-error
opportunities; the observed wrong-but-CRC-valid intermediate frame was on
the hard path, so it is not attributed to the list algorithm without evidence.

The exact rejected intermediate request ended in `ac0b`; the responder
logged `cc0b` and returned those differing bytes. The initiator rejected the
reply. [The original transcript](../evaluation/data/arduino-ping-pong-reference-ab.json)
preserves this event; it is excluded from successful exchange counts.

## Retained development evidence

| Separate dataset | Observed result |
|---|---|
| First Arduino echo with working RX | 8/8 MCU packets; 0/8 independent CRC-valid ACKs |
| Float/FPU transmitter matrix | 19/20 strict LR2021 packets; all four 255-byte settings passed |
| 16-bit capture, hard decoder triad | 16/16 LR2021-directed cases; 3/8 ESP32 peer cases |
| Soft-FEC intermediate triad | 21/24 across six links; 6/8 peer cases |
| Fixed-window standalone pair | 0/4 unique exchanges; late copy deadlines |
| Clock-reacquire standalone pair | 0/4 unique exchanges; same late deadlines |
| Adaptive-window first pair | 2/4 unique exchanges, zero observed late segments |
| Reference-offset A initiates | 2/4 unique exchanges; 32 request + 24 reply copies; one incorrect CRC-valid request |
| Reference-offset B initiates | 3/4 unique exchanges; 32 request + 32 reply copies |

These use different firmware and fresh payloads. They show engineering
progress and failures, not a controlled causal comparison. Raw JSON files
remain in [evaluation/data](../evaluation/data). The earlier native SDK
measurements remain in [their report](native-report.md).

## Software checks and build reproducibility

The portable C++ tests cover 144 codec round trips (SF7–12, CR4/5–4/8,
lengths 1–255), truncation, 24 damaged-CRC negatives, 24 soft-recovery cases,
four soft CRC negatives and four bounded-list recovery cases. Saved real
LR2021 IQ at SF7/8/9 and two actual ESP32 RF captures are replayed, with
truncation, zero and noise negatives. API atomic configuration and errors
are checked; the seven host regression tests pass. These are software and
offline RF checks, **not additional live delivered packets**.

All five Arduino profiles, three native profiles and two stock Arduino
profiles compile. Build/source hashes identify each measured image. The
52 local component inputs are compared against the Windows ASCII staging
checkout with CRLF normalized to LF before a build manifest is recorded.
This demonstrates source identity, not identical cross-machine binaries.
CI compilation and fixture replay are separate from live hardware evidence.

The final source fingerprint is
`ee15d804513597eec83b887fd944680db1f1f8a48c35192a912aad2b1cd077ec`.
[The build manifest](../evaluation/data/arduino-build-final.json) contains all
52 input hashes and all five profile images. Application identities:

| Profile | Bytes | SHA-256 prefix (full value in manifest) |
|---|---:|---|
| rx | 606912 | `10597ff67699a43e` |
| echo | 615072 | `3d694f8f643a68b0` |
| bench | 655120 | `f8646ed4265826c6` |
| ping | 616160 | `5b6f7956513e4eed` |
| pong | 615136 | `9d5fbe12fdf3efb3` |

The independent LR2021 application is 320928 bytes, SHA-256
`a46cbeacaf9a5dcd3d49e40d09996ef9d0791517f9e96a84548ae474596ad6ca`.
Images were written with esptool and its flash hash verification passed;
the manifests hash locally built applications, not private NVS readbacks.

To reproduce, build [ArduinoDuplex](arduino-rx.md), flash both boards with its
`bench` profile, then use [the evaluation commands](../evaluation/README.md).
Give every new run a fresh output path. For autonomous pair tests, flash the
responder first and initiator last, start the observer within the 10 s boot
delay, and keep the optional website disconnected from all three ports.

## Remaining boundaries and license

RX captures 50–900 ms and then decodes; it is half-duplex with blind time.
SF7–12 RX is implemented, but high-SF evidence is only short bench packets.
SF7 TX interoperates; SF8/9 TX remains unreliable and SF10–12 TX returns
`Unsupported`. Continuous reception, CAD, Wi-Fi/BLE coexistence, calibrated
dBm, LoRaWAN and the proposed weak-signal recovery experiment are unfinished.
This report is not a published paper or a claim of world-first LoRa synthesis.

The linked library/backend and examples retain GPL-3.0-only, upstream
provenance and required notices. See [LICENSE](../LICENSE),
[source credits](../SOURCE_LICENSES.md) and [third-party notices](../THIRD_PARTY.md).
Corresponding source and build files accompany this repository; private
AeroLink source, NVS readbacks and device backups are excluded.
