# One ESP32-S3, native LoRa packets

The ESP32 performs capture, synchronization, demodulation, header decoding,
error correction, dewhitening and payload CRC itself. It also encodes and
transmits its own packets through its internal 2.4 GHz RF chain. Your program
gets complete bytes. A computer can show logs; it is not the packet decoder.

## Start with the tested board

Use a **Seeed XIAO ESP32-S3 with 8 MB flash and 8 MB OPI PSRAM**, its connected
2.4 GHz antenna, and a USB data cable. Install Python, Git and PlatformIO.
On Windows, clone into an ASCII-only path such as `C:\lora-sdr`.

From the repository root:

```sh
python examples/NativeDuplex/setup_deps.py
pio run -d examples/NativeDuplex -e xiao-native
pio device list
pio run -d examples/NativeDuplex -e xiao-native -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

The project pins `espressif32@7.0.1` with ESP-IDF 6.0.1 and a specific public
ESP-DSP revision. Its SDK configuration reserves RF SRAM, enables OPI PSRAM,
runs the RTOS on core 0 and dedicates core 1 to bounded acquisition. Use the
supplied project configuration. An ordinary stock Arduino sketch currently
supports TX only; its `receive()` returns `Unsupported`. Full native RX/TX is
the **PlatformIO ESP-IDF component** route above.

## The API your application calls

```cpp
#include <LoRaRadio.h>
using namespace lora_sdr;
LoRaRadio radio;

// In app_main(), after checking each result:
radio.begin(2440.125);             // MHz
radio.setSpreadingFactor(7);
radio.setBandwidth(203.125);       // kHz
radio.setCodingRate(5);            // denominator: 5 means 4/5
radio.setPreambleLength(16);
radio.setSyncWord(0x12);
radio.setTransmitPowerPercent(50); // DAC amplitude; uncalibrated, not dBm

Error sent = radio.send("Hello from ESP32-S3!");
RxPacket packet;
Error received = radio.receive(packet, 500);
if (received == Error::Ok) {
    // packet.payload[0..packet.length) contains the CRC-valid bytes.
    // Binary payloads need not have a terminating zero.
}
```

Call `send()` only when your application intends to transmit. The default
example receives and never sends at boot. Check the returned `Error` rather
than assuming a setter or transmission succeeded. `send()` means the local
operation completed; delivery needs a valid packet at the other radio.

| Setting | Interface and current RF boundary |
|---|---|
| Frequency | `setFrequency(MHz)`, 2400.2–2483.3; RF tune resolution 1 kHz |
| Spreading factor | `setSpreadingFactor(7..12)`; native RF RX demonstrated at SF7–12, with only one 8-byte trial each at SF10–12 in the final matrix. TX SF7 demonstrated; SF8/9 experimental and unreliable, SF10–12 return `Unsupported` |
| Coding rate | `setCodingRate(5..8)` selects 4/5..4/8; explicit RX reads CR from the packet header |
| Bandwidth | TX 203.125/406.25/812.5 kHz, with automatic matching DAC window; native RX currently **203.125 kHz** |
| Length | `send(bytes, length)`, 1..255; the receiver extracts length from the explicit header. A complete packet must fit its capture window |
| Preamble / sync | `setPreambleLength(12..64)`, `setSyncWord(byte)`; both radios must match sync |
| TX level | `setTransmitPowerPercent(1..100)` maps to DAC peak 2..200; no calibrated dBm API |
| Frequency correction | `setFrequencyCorrection(Hz)`, ±50 kHz. Default +15 kHz was measured for this pair |
| Receive window | `receive(packet, 50..900 ms)`, captures then decodes one complete CRC-valid packet |

## Independent receive-and-reply demonstration

The explicitly selected `xiao-echo` environment adds one behavior: after a
CRC-valid packet, it sends `ACK:` followed by the same received bytes. The
default `xiao-native` environment stays receive-only. No ESP32 command or
expected-payload hint is required to perform the echo.

```sh
pio run -d examples/NativeDuplex -e xiao-echo -t upload --upload-port YOUR_XIAO_PORT
pio run -d companion/lr2021 -e aerolink-hf-tx -t upload --upload-port YOUR_LR2021_PORT
python evaluation/verify_native_echo.py --xiao YOUR_XIAO_PORT --lr2021 YOUR_LR2021_PORT --output echo-result.json
```

The LR2021 companion pinout is for the verified AeroLink board; see its
[hardware instructions](../companion/lr2021/README.md). A different LR2021
board needs its correct pinout and RF switch configuration.

## Understand the receive timing

This is half-duplex, finite-window reception. During a 500 ms acquisition,
the receiver retains contiguous **250,000 complex samples/s** with 8-bit I
and 8-bit Q in PSRAM. Early 4-bit tests established packet reception; the
latest native FIR batching allows the higher precision. A complete CRC-valid packet
can be extracted from a contiguous prefix if the capture stops at a ring
boundary; `lastReceive().captureStatus` preserves that fact.

Decoding follows capture and creates a period when new RF is not received.
The native API currently has substantial processing latency, especially at
SF9. It is not a continuously listening replacement for a LoRa transceiver.
Run one radio object from one task, without Wi-Fi/BLE coexistence. A timeout
means no complete CRC-valid packet was found, not proof that the channel was
silent. `lastReceive()` exposes capture and decoder diagnostics.

The [native test report](native-report.md) preserves passes, failures, exact
bytes, CRC gates, timing and firmware identification. It distinguishes this
native path from the earlier separate PC-IQ decoder measurements.
