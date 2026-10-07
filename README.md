<p align="center"><img src="docs/assets/hero.svg" alt="ESP32 LoRa SDR — an application library using the radio inside ESP32-S3" width="100%"></p>

# ESP32 LoRa SDR

**A C++ library for complete 2.4 GHz LoRa packets using the RF already inside your ESP32-S3.**

Your application calls `begin()`, sets the radio parameters, then calls
`transmit()` or `receive()`. The ESP32 performs waveform generation,
demodulation, error correction and packet CRC. It needs no external LoRa
chip, website or PC packet decoder. A second radio is the peer on the air.

[Arduino RX/TX](docs/arduino-rx.md) · [API](docs/api.md) · [中文](README.zh-CN.md) ·
[Measurements](docs/arduino-report.md) · [Prior art](docs/prior-art.md)

Experimental; verified on **Seeed XIAO ESP32-S3, 8 MB flash + 8 MB OPI PSRAM**.
**Arduino RX/TX is available through the supplied PlatformIO ArduinoDuplex
profile**, using real `setup()` / `loop()` and Arduino 2.0.17 as an IDF 4.4.7
component. Packet decoding and CRC stay on the MCU. An ESP-IDF-only profile
is also included. Installing just the ZIP into a stock Arduino core provides
TX only; use the duplex project for RX. The backend uses undocumented RF
registers and SDK PHY routines, so other boards and SDKs require verification.

## Use it from your application

The units and setter names follow familiar LoRa library conventions:
MHz, kHz, spreading factor and coding-rate denominator. Check returned errors.

```cpp
#include <LoRaRadio.h>
using namespace lora_sdr;

LoRaRadio radio;
LoRaSettings settings;
settings.frequencyMHz = 2440.125;
settings.bandwidthKHz = 203.125;
settings.spreadingFactor = 7;
settings.codingRate = 8;              // 4/8; choose 5, 6, 7 or 8
settings.transmitPowerPercent = 75;   // relative amplitude, not dBm

// Inside your application:
Error status = radio.begin(settings);
if (status != Error::Ok) return;

status = radio.transmit("Hello from ESP32-S3!");
// Local TX completion is not an acknowledgment from the peer.
RxPacket packet;
status = radio.receive(packet, 500);  // 500 ms capture, then native decoding
if (status == Error::Ok) {
    // Use packet.payload[0..packet.length): complete CRC-valid bytes.
}
```

Change settings with `setFrequency()`, `setBandwidth()`,
`setSpreadingFactor()`, `setCodingRate()`, `setPreambleLength()`,
`setSyncWord()` and `setTransmitPowerPercent()`. `configure(settings)`
validates a whole profile before applying it. `transmit(bytes, length)` handles
binary payloads of 1–255 bytes; `send()` is an equivalent alias.

This is a separate library with a [RadioLib-inspired call style](docs/api.md).
It is not a drop-in SX1262 driver. TX power has not been calibrated in dBm.

## Build a standalone receiver or receive-and-reply application

Install Python, Git and PlatformIO. Download the ZIP or clone the repository
with your GitHub access; it is private during development. On Windows use
an ASCII path such as `C:\lora-sdr`. From the repository root:

```sh
python -m pip install platformio==6.1.19
python examples/NativeDuplex/setup_deps.py
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx
pio device list
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx -t upload --upload-port YOUR_PORT
```

Edit [the Arduino application](examples/ArduinoDuplex/main/main.cpp) to use the library
in your own project. `xiao-arduino-rx` receives by default. Select `xiao-arduino-echo` to
receive a CRC-valid packet and transmit `ACK:` plus those bytes entirely on
the ESP32. After flashing, USB logs are optional; RF packet processing runs
on the board. Follow the [Arduino guide](docs/arduino-rx.md) for the required
PSRAM/core settings, peer configuration and timing. It also includes bounded
`xiao-arduino-ping` / `xiao-arduino-pong` applications for two ESP32s.
Prefer `app_main()`? Use the separate [native project](docs/native-guide.md).

For stock Arduino TX, install the repository ZIP as a library, select XIAO
ESP32-S3 with core 2.0.17, and open
[SendOnce](examples/SendOnce/SendOnce.ino). The
[Arduino guide](docs/quick-start.md) also covers its PlatformIO build.

## Measured capabilities

| Operation | Verified scope and current limit |
|---|---|
| Arduino ↔ LR2021 | Both ESP32s transmit and receive: **16/16** cases across four directed links, SF7, CR4/5 and 4/8, 8/32 bytes. |
| ESP32 ↔ ESP32 | **6/8** host-scheduled single-packet cases; all four CR4/8 cases passed. Separate autonomous sessions: **3/4** unique round trips in each role assignment, with eight explicitly counted RF copies per request/reply. |
| Arduino transmit | SF7, CR4/5–4/8, 1–255 bytes: **18/20** strict CRC + exact bytes; all four 255-byte cases passed. Misses retained. |
| Arduino receive | 203.125 kHz, SF7–12: **6/6** fresh 8-byte packets; four negative checks rejected. This is a short-packet smoke test. |
| Standalone Arduino echo | **8/8** MCU receptions and **8/8** independent LR2021 CRC-valid exact ACKs; zero ESP32 serial commands. |
| Parameters | Frequency, SF, CR, bandwidth, preamble, sync, relative amplitude and frequency correction; see [supported ranges](docs/native-guide.md#the-api-your-application-calls). |

![Six RF links and autonomous Arduino ping/pong](docs/assets/arduino-duplex.svg)

These are separate measurements from one stationary indoor three-radio bench,
not a reliability or range guarantee. Raw payloads, failures, IRQ/CRC gates,
firmware hashes and figures are in the [Arduino report](docs/arduino-report.md).
One intermediate MCU output passed CRC but differed from the transmitted
payload; the autonomous initiator rejected its reply. Applications needing
stronger integrity should add an application checksum or authenticated framing.
Earlier native SDK results are in the [native report](docs/native-report.md),
and Arduino/PC-IQ investigations remain in the
[historical report](docs/test-report.md).

Reception captures finite 50–900 ms windows at 250 kcomplex samples/s, then
decodes on the ESP32. It is half-duplex, with blind time during decoding;
higher SFs can take tens of seconds. TX SF8/9 is unreliable and SF10–12 is
unsupported. Wi-Fi/BLE coexistence, calibrated output power, continuous RX,
CAD and LoRaWAN are not implemented.

## Optional debugging

The serial bench can display complete packets in a two-board browser UI.
[The bench proof and instructions](docs/native-report.md#native-tx-parameters-and-receiver-epochs)
are optional development tools. The library, standalone receiver and echo
example have no browser or Python runtime dependency on the ESP32.

## How it works and credit

The encoder creates the explicit header, payload CRC, whitening, Hamming
coding, interleaving and LoRa symbols. The backend plays I/Q windows through
the S3 internal 2.4 GHz RF chain; native RX retains IQ in PSRAM and decodes
complete packets in C++. The current DAC waveform has gaps between windows.
It is not continuous, gap-free LoRa transmission.

This project builds on [ESPARGOS](https://github.com/ESPARGOS/esp-sdr),
[Jochen Hammes](https://github.com/jochenhammes/esp32-sdr-trx/tree/research/iq-tx)
and [CNLohr](https://github.com/cnlohr/lolra). It is not presented as the first
firmware-generated LoRa transmitter. [Prior-art notes](docs/prior-art.md)
explain reused work and failed approaches. GPL-3.0-only;
[third-party notices](THIRD_PARTY.md). Use RF settings permitted for your
location and hardware.
