# Arduino receive and transmit

`ArduinoDuplex` is a real Arduino application: `setup()`, `loop()`, `Serial`
and the same `LoRaRadio` API as the transmitter. PlatformIO builds Arduino
2.0.17 **as an ESP-IDF 4.4.7 component** so the receiver can own its required
RF memory and acquisition core. Packet synchronization, demodulation,
deinterleaving, error correction and payload CRC run on the ESP32-S3.

This build profile is required for RX. Installing the ZIP into the stock
Arduino IDE does not rebuild that core's SDK or change its CPU configuration;
that existing TX-only route still returns `Unsupported` for RX. Do not enable
`LORA_SDR_NATIVE_BACKEND` manually in a stock sketch: the macro describes a
linked backend and does not supply one.

## Build and upload

Use a Seeed XIAO ESP32-S3 with 8 MB flash, 8 MB **OPI PSRAM**, its 2.4 GHz
antenna and a USB data cable. On Windows extract into an ASCII path such as
`C:\lora-sdr`. From the repository root:

```sh
python -m pip install platformio==6.1.19
python examples/NativeDuplex/setup_deps.py
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx
pio device list
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

Close other applications using that USB port first. The first build downloads
the pinned Arduino and IDF packages. PlatformIO may print a generic warning
about the default Arduino variant; this project explicitly sets
`CONFIG_ARDUINO_VARIANT="XIAO_ESP32S3"` in its SDK defaults.

Edit [the Arduino sketch](../examples/ArduinoDuplex/main/main.cpp). Its default
is receive only. `xiao-arduino-echo` explicitly opts into transmitting `ACK:`
plus a received packet of up to 251 bytes. No packet is transmitted at boot.
After upload, a computer or website is unnecessary for RF packet processing;
USB output is just the application's logging.

## Two ESP32s

The bounded ping/pong examples use the same library. Build both, flash the
responder first, then the initiator:

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-pong -e xiao-arduino-ping
pio run -d examples/ArduinoDuplex -e xiao-arduino-pong -t upload --upload-port RESPONDER_PORT
pio run -d examples/ArduinoDuplex -e xiao-arduino-ping -t upload --upload-port INITIATOR_PORT
```

The initiator waits 10 seconds, then attempts four 20-byte requests with a
random session token, sequence and random bytes. The responder replaces
`PING` with `PONG` only after full CRC-valid reception. The initiator accepts
only a CRC-valid reply matching the entire request token and sequence.
The initiator sends eight explicit copies of each request, 350 ms apart.
After decoding a request, the responder waits 4.5 s, then sends eight copies
of its reply, also 350 ms apart. These bounded trains cover receive/decode
blind intervals; they are separate RF transmissions of one unique packet.
The initiator listens in 500 ms windows for up to 18 s and waits 5.5 s before
the next request. Every copy is logged; the library does not retry silently.
After four unique requests the initiator stops transmitting. Power-cycle
it to start another session. Both boards can run from USB power supplies;
monitor output is optional. Consult the RF report for actual success rates.

`xiao-arduino-bench` is an optional host-controlled scheduling fixture for
cross-checking both ESP32s against an independent LR2021. It also decodes
packets on the ESP32; it is not required by applications or the pair examples.

```cpp
#include <Arduino.h>
#include <LoRaRadio.h>
using namespace lora_sdr;

LoRaRadio radio;
bool ready = false;

void setup() {
  Serial.begin(115200);
  LoRaSettings settings;
  settings.frequencyMHz = 2440.125;
  settings.bandwidthKHz = 203.125;
  settings.spreadingFactor = 7;
  settings.codingRate = 5;             // TX coding rate 4/5
  settings.transmitPowerPercent = 75;  // relative amplitude, not dBm
  ready = radio.begin(settings) == Error::Ok;
}

void loop() {
  if (!ready) { delay(1000); return; }
  RxPacket packet;
  Error status = radio.receive(packet, 500);
  if (status == Error::Ok) {
    Serial.write(packet.payload, packet.length); // binary-safe, CRC verified
    // Optional reply:
    // status = radio.transmit(packet.payload, packet.length);
  }
  delay(10);
}
```

## Peer and operating limits

The independent receiver/transmitter used for testing is the public
[LR2021 HF companion](../companion/lr2021/README.md). Set its frequency to
2440.125 MHz, bandwidth to 203.125 kHz, SF7, preamble 16, sync `0x12`,
standard IQ, explicit header and payload CRC. The explicit header carries
the received payload's coding rate and length.

RX currently supports 203.125 kHz, SF7–12 and finite capture windows of
50–900 ms. The MCU decodes after capture; it cannot listen while decoding or
transmitting. Higher SFs can take seconds to tens of seconds to decode.
The Arduino RF profile must be tested separately from the newer native SDK
profile; see the measurement report before relying on a setting.

The supplied SDK runs the RTOS and Arduino loop on core 0 and reserves core 1
for acquisition. It reserves three RF SRAM banks, uses OPI PSRAM for IQ and
disables interrupt/task watchdogs because bounded capture masks interrupts.
Keep Wi-Fi/BLE off and do not create application tasks on core 1. These are
experimental SDR requirements, not ordinary Arduino defaults.
The example fixes the Arduino loop stack at 8 KiB and reduces unused Wi-Fi
buffer pools so the capture worker can allocate its contiguous RF workspace.
Keep this override when adapting the sketch; a larger stack can silently
prevent startup under the RF SRAM reservation.

The integration follows Espressif's
[Arduino component workflow](https://espressif-docs.readthedocs-hosted.com/projects/arduino-esp32/en/latest/esp-idf_component.html).
The library's [API documentation](api.md) covers returned errors, settings and
diagnostics. The website is an optional separate serial-bench application.
