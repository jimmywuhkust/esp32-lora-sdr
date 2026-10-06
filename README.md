<p align="center"><img src="docs/assets/hero.svg" alt="ESP32 LoRa SDR — real packets from the RF inside ESP32-S3" width="100%"></p>

# ESP32 LoRa SDR

**Send a complete 2.4 GHz LoRa packet using the RF already inside an ESP32-S3.**
The sender needs no external LoRa transceiver. An independent LR2021 receives
the bytes and checks the packet CRC.

[English quick start](docs/quick-start.md) · [中文说明](README.zh-CN.md) ·
[Measured results](docs/test-report.md) · [API](docs/api.md) · [Prior art](docs/prior-art.md)

This is an **experimental radio library**, tested on a Seeed XIAO ESP32-S3.
It uses undocumented RF registers and SDK PHY routines. The measured bench
profile is 2440.125 MHz, 203.125 kHz bandwidth, SF7, private sync `0x12`.
Read the capability table before choosing other settings.

## What is real today?

| Capability | Evidence / limit |
|---|---|
| XIAO → LR2021 complete packet | Independent hardware CRC and exact-byte comparison |
| On-device coding and transmission | Arduino / PlatformIO; the PC supplies payload bytes, not an I/Q waveform |
| SF7, four coding rates, 1–255 bytes | Final strict IRQ matrix: **224/240 accepted, 240/240 exact payloads**. Sixteen ambiguous header-error events rejected. Earlier API-based public matrix 239/240; [criteria and raw evidence](docs/test-report.md#final-receiver-irq-audit-stricter-hardware-evidence) |
| Portable PHY encoder | 264/264 on-device cross-checks: SF7–12, CR4/5–4/8, lengths 1–255; this is **coding verification**, not RF verification |
| SF7 at 406.25 / 812.5 kHz | Separate randomized matrices: 72/72 at each bandwidth, four coding rates, lengths 1–255; matching windows are required |
| RF parameter selection | Frequency, preamble, coding rate, frequency correction, DAC amplitude and waveform window; unsupported combinations return an error |
| Channel, preamble, sync and IQ polarity | 142/144 fresh 32-byte CRC packets across 48 settings; both failures retained. Three channels, four preambles, sync0x12/0x34 and both matched polarities |
| SF8 / SF9 transmission | Windowed DAC has failed; PLL has delivered some exact CRC packets but only 1/10 SF8 and 4/10 SF9 in small diagnostic runs. Unreliable and experimental |
| Analog level control | Optional raw PBUS codes; a constant-DAC seven-code study delivered 70/70. Codes are nonmonotonic and uncalibrated; see the report |
| Receive on XIAO | Public standalone IQ capture + **PC** full decoder: **104/108** live packets, SF7/8/9, four CRs, lengths1/8/32. [Capture source, live script and real recordings](host/README.md) included. Native Arduino packet RX is not implemented |
| Calibrated TX power, distance or sensitivity | Not measured; raw gain and amplitude are not dBm |

The measurements are from one stationary indoor board pair. Failed tests,
resets and mismatched packets stay in the report.

The reverse path requires the separate ESP-IDF capture application on XIAO,
plus a PC decoder. It does not run inside the Arduino transmitter sketch.
The default capture matrix retained99.18–100% of IQ within500ms windows;
USB-frame drops are explicit. There is no continuous4MS/s USB claim.

![Two separately measured hardware datasets](docs/assets/public-receiver-results.svg)

![Native XIAO transmission and independent LR2021 reception, including Chinese UTF-8 and exact bytes](docs/assets/final-live-proof.png)

## Your first packet

Use [SendOnce](examples/SendOnce/SendOnce.ino) or the USB
[SerialBench](examples/SerialBench/main.cpp). No sketch in this project sends
automatically at boot: type a command to start a bounded transmission.

```cpp
#include <LoRaSDR.h>
#include <ESP32S3Radio.h>

lora_sdr::ESP32S3Radio radio;
lora_sdr::Config config;

// After radio.begin() returns Ok:
config.transport = lora_sdr::Transport::DacWindows;
config.spreadingFactor = 7;
config.codingRate = 4;                 // 4/8
const uint8_t message[] = "Hello from XIAO!";
lora_sdr::TxResult result;
auto status = radio.transmit(message, sizeof(message)-1, config, result);
// status reports the TX operation. Only the independent receiver proves delivery.
```

PlatformIO:

```sh
pio run -e xiao-s3
pio device list
pio run -e xiao-s3 -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

Then enter `INFO`, `DAC`, and
`TX 7 4 48656c6c6f2066726f6d205849414f21`. Configure the LR2021 receiver
to the matching profile and look for **CRC OK + the exact same hex bytes**.
Use the included [public LR2021 companion](companion/lr2021/README.md) for a
complete receiver setup and [automated verification](evaluation/verify_public_receiver.py).
The +15 kHz correction in the defaults was measured for one bench pair; it is
not a calibration value for every ESP32.

## How it works

The PHY encoder creates the header, whitening, payload CRC, Hamming coding,
diagonal interleaving and Gray-mapped symbols. The S3 backend keys its internal
2.4 GHz RF chain and plays I/Q windows from RF SRAM at 40 MS/s.

The current DAC waveform includes silent gaps between symbol windows. For the
default SF7 profile, 15,000 of about 25,206 samples are played per full up-chirp
(about 59.5%). This is **not continuous, gap-free LoRa transmission**. There
is also a PLL modulation backend for research and comparison; its randomized
payload reliability is lower on this bench.

## Reproducibility and credit

Research builds, seeds, payload hex, received hex, CRC results, timing failures
and firmware hashes accompany the measurements. The test report distinguishes
host-generated waveforms from native Arduino transmission and software checks
from independent RF tests.

This project follows earlier work by [ESPARGOS](https://github.com/ESPARGOS/esp-sdr),
[Jochen Hammes](https://github.com/jochenhammes/esp32-sdr-trx/tree/research/iq-tx)
and [CNLohr](https://github.com/cnlohr/lolra). **It is not presented as the first
firmware-generated LoRa transmitter.** Their failed experiments are useful
engineering evidence; [our prior-art notes](docs/prior-art.md) explain what
was reused and what was measured here.

GPL-3.0-only. See [LICENSE](LICENSE) and [third-party notices](THIRD_PARTY.md).
Use RF settings permitted for your location and connected hardware.

The optional PSRAM queue retained all output IQ in a completed 36-window block (34 full CRC packets), but longer batches still aborted at ring edges. [Actual IQ, failures and retention figures](docs/test-report.md#final-finite-window-capture-validation).
