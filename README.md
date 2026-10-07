<p align="center"><img src="docs/assets/hero.svg" alt="ESP32 LoRa SDR — an application library using the radio inside ESP32-S3" width="100%"></p>

# ESP32 LoRa SDR

Send and receive 2.4 GHz LoRa packets with the radio inside an ESP32-S3.
Packet encoding, decoding and CRC run on the board; no external LoRa chip is needed.

[Get started](docs/arduino-rx.md) · [API](docs/api.md) · [中文](README.zh-CN.md) ·
[Test results](docs/arduino-report.md) · [Prior art](docs/prior-art.md)

## Get started

Use a XIAO ESP32-S3 with 8 MB flash, 8 MB OPI PSRAM and its 2.4 GHz antenna.
You also need a second ESP32 running this project, or a compatible 2.4 GHz
LoRa radio, to send or receive packets.

For **send and receive**, start with the included PlatformIO project.
**Arduino IDE currently supports sending only**; installing the library ZIP
will not enable reception. See [the Arduino IDE instructions](docs/quick-start.md)
if you only need to transmit.

Download and extract the repository, or clone it with your GitHub account.
On Windows, use a folder such as `C:\lora-sdr`. Open a terminal in the
directory containing `README.md` and `platformio.ini`:

```sh
python -m pip install platformio==6.1.19
python examples/NativeDuplex/setup_deps.py
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx
pio device list
```

Find your board's port in the list, replace `YOUR_PORT`, then upload and open
the serial monitor:

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

The example listens for packets. Set the other radio to **2440.125 MHz,
203.125 kHz bandwidth, SF7, preamble 16, sync `0x12`, explicit header and
payload CRC**. Received bytes appear after `ARDUINO_RX crc_ok=1`.

With two ESP32s, follow [the ping/pong instructions](docs/arduino-rx.md#try-two-esp32s)
to send a request and receive a reply. Start there before changing radio settings.

## Write your application

Edit [`examples/ArduinoDuplex/main/main.cpp`](examples/ArduinoDuplex/main/main.cpp).
Use `setup()` to start the radio and `loop()` to send or receive. The
[guide includes a complete receiver](docs/arduino-rx.md#write-your-own-program)
you can copy into that file.

These are the calls you use after `radio.begin(settings)` succeeds:

```cpp
Error status = radio.transmit("Hello from ESP32-S3!");

RxPacket packet;
status = radio.receive(packet, 500);
if (status == Error::Ok) {
    Serial.write(packet.payload, packet.length);
}
```

`receive(packet, 500)` captures for 500 ms, then decodes the recording.
`transmit()` returning `Ok` means the board finished sending; check the
other radio to confirm delivery.

Set frequency in MHz, bandwidth in kHz and coding rate as `5`, `6`, `7` or
`8` for 4/5 through 4/8. Start with SF7 and coding rate `8`. Power is a
relative setting from 1 to 100, not dBm. See [the API](docs/api.md) for all
settings and return values.

For an ESP-IDF application using `app_main()`, use [NativeDuplex](docs/native-guide.md).

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
