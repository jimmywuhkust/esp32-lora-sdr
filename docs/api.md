# Library API and capability boundaries

Namespace: `lora_sdr`. Include `LoRaSDR.h` for portable coding and
`ESP32S3Radio.h` for the Arduino ESP32-S3 transmitter.

## Simple RX/TX interface

Include `LoRaRadio.h`. `LoRaRadio` exposes `begin(MHz)`, `setFrequency(MHz)`,
`setSpreadingFactor(7..12)`, `setBandwidth(kHz)`, `setCodingRate(5..8)`,
`setPreambleLength(12..64)`, `setSyncWord(byte)`,
`setFrequencyCorrection(Hz)`, `setTransmitPowerPercent(1..100)`,
`transmit(bytes, length)` / `transmit(text)`, and `receive(RxPacket&, windowMs)`.
`send()` is an equivalent alias. `begin(LoRaSettings)` validates a complete
application profile before initialization; `configure(LoRaSettings)` updates
it atomically without emitting RF. An invalid profile leaves settings unchanged.
Every call returns `Error`. Changing bandwidth selects a matching DAC window.
See [the native guide](native-guide.md) for the measured SF/bandwidth limits.
Setter ranges alone are not an RF guarantee.

`RxPacket` owns up to 255 bytes and carries decoded length, SF, CR, payload
CRC and correction metadata. `lastReceive()` reports capture status, samples,
drops, abandoned units and decode time. Native receive needs the IDF component;
stock Arduino receive returns `Unsupported`.

The call style follows the [RadioLib SX126x settings example](https://github.com/jgromes/RadioLib/blob/master/examples/SX126x/SX126x_Settings/SX126x_Settings.ino):
frequency in MHz, bandwidth in kHz, SF, coding-rate denominator and explicit
return-status checks. This is a separate library, not a RadioLib module or a
drop-in SX1262 driver. `setTransmitPowerPercent()` controls relative amplitude;
there is no calibrated `setOutputPower(dBm)` or supported continuous/interrupt
RX, CAD, LoRaWAN, sleep or RSSI API. Refer to the
[SX126x driver reference](https://jgromes.github.io/RadioLib/class_s_x126x.html)
for the broader external-transceiver API; those functions cannot be assumed here.

## Encoder

```cpp
Error Encoder::encode(const uint8_t* payload, size_t length,
                      const Config& config, uint16_t* symbols,
                      size_t capacity, PacketInfo& info);
uint16_t Encoder::crc16(const uint8_t* payload, size_t length);
```

The encoder allocates no heap memory and accesses no radio. It rejects null
buffers, empty payloads and payloads longer than 255 bytes. The caller supplies
the symbol buffer; 1,024 entries cover supported configurations. Too little
space returns `BufferTooSmall`. `PacketInfo` contains symbol count, payload CRC,
automatic LDRO selection and calculated airtime.

CRC uses the LoRa PHY convention including its final two-byte XOR; it is not
the USB transport CRC32. The explicit header includes its own checksum.
The actual on-device cross-check covers explicit header + CRC enabled only.
Implicit header and CRC-off coding exist in the API but remain unverified.

## Radio

```cpp
Error ESP32S3Radio::begin();
Error ESP32S3Radio::transmit(const uint8_t*, size_t,
                           const Config&, TxResult&);
```

`begin()` initializes and calibrates the PHY at 240 MHz CPU. It creates a
minimal Wi-Fi NULL-mode driver, disables power save and initializes promiscuous
mode without associating to a network. Wi-Fi and BLE application coexistence
is not supported. Initialization does not transmit a test packet automatically.
RF transmission blocks until that one bounded packet completes.

The **native ESP-IDF component** implements `receive()` with capture into
PSRAM and full on-device CRC-valid packet decoding. The overload accepts
`Config`, `RxResult` and a 50..900 ms capture window. `ReceiveTimeout` means no
complete CRC-valid packet was found, `CaptureGap` means invalid IQ continuity.
A valid packet from a contiguous prefix can succeed after a boundary stop;
`RxResult.captureStatus` preserves that stop. Stock Arduino RX remains
`Unsupported`.

| Config field | Meaning | Native RF boundary |
|---|---|---|
| `frequencyHz` | RF channel in Hz | 2400.2–2483.3 MHz, configured in 1 kHz steps for DAC |
| `bandwidthHz` | LoRa bandwidth | TX SF7: 203125, 406250, 812500 with matching windows; native RX: 203125 only |
| `spreadingFactor` | PHY SF | Native RX SF7–12 tested; TX SF7 demonstrated, SF8/9 unreliable, SF10–12 returns `Unsupported` |
| `codingRate` | 1, 2, 3, 4 | 4/5, 4/6, 4/7, 4/8 |
| `syncWord` | Two sync nibbles | Default `0x12`; `0x12` and `0x34` covered by the settings matrix |
| `preambleSymbols` | Preamble chirps | RF 12–64, default 16 |
| `explicitHeader` / `payloadCrc` | Packet modes | RF requires both true |
| `inverted` | S3 physical chirp convention | true matches LR2021 standard IQ; false matches LR2021 inverted IQ |
| `frequencyCorrectionHz` | LO correction | ±50 kHz; +15 kHz measured for one board pair |
| `transport` | `Pll` or `DacWindows` | DAC is the default and the measured profile |
| `dacAmplitude` | Signed 10-bit I/Q peak | 1–200, default 150; uncalibrated |
| `dacWindowSamples` | Played up-chirp samples | 1000–16380 and shorter than the symbol; SF7: 15000 / 7500 / 3750 for the three bandwidths |
| `gainCode` | Vendor tone gain code | 64–200; not dBm and does not calibrate DAC power |
| `analogGainCode` | Experimental PBUS level code | Default 0 keeps keyed defaults; 1–63 for DAC only; nonmonotonic, uncalibrated and never above either measured keyed default |
| `updateRateHz` | PLL frequency update rate | 40–200 kHz, exact divisor of 240 MHz |

The DAC backend reserves RF SRAM bank 2 for playback and checks linked RAM
boundaries before initialization. It requires internal RAM for its waveform
and returns `NoMemory` if allocation fails. SF8/SF9 use compact phase tables;
successful coding and timing do not establish LR2021 compatibility.
DAC airtime is capped at one second; PLL airtime is capped at 250 ms because
its interrupt-masked loop cannot service the SDK watchdog. The original independent bench firmware reports
payload lengths up to 250 bytes; the later public receiver verified 255-byte
packets, including all forty full-length trials in its matrix.

`TxResult` records coding metadata, update count, late updates, source buffer
address and maximum copy cycles. The watchdog is serviced between symbol
windows. A DAC engine timeout returns `PlaybackTimeout` and stops playback.
Late counters are diagnostic evidence, not a receiver acknowledgment.

For explicit analog gain, `TxResult` also records both keyed defaults and
both programmed readbacks, and `analogGainRestored`. The backend verifies
the requested values and restoration; a failed transaction or restoration
returns `NotReady`. It rejects this setting on PLL before keying RF. Do not
interpret a register code as a linear power scale. `analogGainCode=0` skips
PBUS changes entirely and keeps the previously measured default path.

## Bench protocol

SerialBench accepts newline-terminated commands:

```text
INFO                 firmware identity
DAC / PLL            select transport
ENC 7 4 HEX          encode only; returns symbols, no RF
TX 7 4 HEX           one native encoded RF packet
AMP 150              raw DAC amplitude, 1–200
GAIN 119             raw tone-gain code, 64–200
PA 0                 preserve keyed analog defaults; experimental PA 1–63 for DAC
BW 203125            203125 / 406250 / 812500 Hz; choose matching window
WIN 15000            DAC window, 1000–16380 samples
PRE 16               preamble, 12–64 symbols
SYNC 18              sync word, decimal 0–255; 18=0x12, 52=0x34
INV 1                S3 waveform polarity; 0/1, match receiver convention
FREQ 2440125         channel in kHz
CFO 15000            correction in Hz
```

Invalid hex, odd-length hex, trailing arguments and out-of-range parameters
are rejected. Settings do not emit RF. There is no automatic retransmission.

The settings matrix tests three channels (2403.125,2440.125,2476.125 MHz),
four preambles (12,16,32,64), two sync words and both matched IQ polarities.
It does not validate every allowed frequency/sync value or their Cartesian
product with every bandwidth, length, CR and gain. Read its actual failures
and denominator in the report rather than assuming 100% interoperability.

The [PC decoder companion](../host/README.md) decodes new live packets and
reproduces real SF7/8/9 recordings from the separate public capture image.
It is not an implementation of `ESP32S3Radio::receive()`.
