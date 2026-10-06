# First packet, step by step

## Hardware and software

- Seeed XIAO ESP32-S3 with its 2.4 GHz antenna attached, USB data cable.
- An independent 2.4 GHz LoRa receiver. The measured receiver is an AeroLink
  LR2021 using its HF port. The public [RadioLib receiver companion](../companion/lr2021/README.md)
  passed 239/240 independent CRC/complete-payload trials. An earlier dataset
  used the privately supplied application; it is not needed for this workflow.
- PlatformIO Core / VS Code, or Arduino IDE with **Espressif Arduino core 2.0.17**.
  Other core versions are unverified; the RF backend uses private SDK functions.
- A serial terminal that sends a newline at the end of each command.

The XIAO uses its own radio. Do not wire an external LoRa module to the sender.
The receiver still needs a real LoRa transceiver for the independent CRC proof.
The AeroLink source snapshot is not redistributed in this repository.

## PlatformIO

Download the repository ZIP, extract it, and open the folder containing
`platformio.ini`. Open a PlatformIO terminal there:

```sh
pio run -e xiao-s3
pio run -e xiao-send-once
pio device list
```

Select the USB port belonging to the XIAO. Windows ports look like `COM3`;
Linux ports often look like `/dev/ttyACM0`. Substitute your actual port:

```sh
pio run -e xiao-s3 -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

Close other software using that port before uploading or opening the monitor.
The connected XIAO was verified to have 8 MB flash, 8 MB OPI PSRAM and a 40 MHz
crystal. A different board/profile must be checked separately.

## Arduino IDE

Copy this repository into the Arduino libraries directory, or install the ZIP
through **Sketch → Include Library → Add .ZIP Library**. Select **XIAO_ESP32S3**,
240 MHz CPU, native USB CDC enabled and the board's correct flash/OPI PSRAM
settings. Open `examples/SendOnce/SendOnce.ino`. This project was built and
hardware-tested through PlatformIO's Arduino 2.0.17 package; the IDE workflow
uses the same core but requires you to select the matching board settings.

## Set the independent receiver

| Setting | Value |
|---|---|
| RF input | HF port / antenna supporting 2.4 GHz |
| Frequency | 2440.125 MHz |
| Bandwidth | 203.125 kHz |
| Spreading factor | 7 |
| Coding rate | 4/8 for the first test |
| Preamble | 16 symbols |
| Sync | Private `0x12` |
| Header | Explicit |
| Payload CRC | Enabled and required |
| Receiver IQ | Standard |

The backend's `inverted=true` compensates the S3 RF path convention; it does
not mean you should enable inverted IQ on the LR2021 receiver.
The measured XIAO LO correction is +15 kHz. Other boards can need a different
value: adjust `Config::frequencyCorrectionHz` and verify with real packets.

## Send one packet

For the SerialBench firmware, enter each line separately:

```text
INFO
DAC
TX 7 4 48656c6c6f2066726f6d205849414f21
```

`INFO` must identify the native library. `DAC` selects windowed DAC modulation.
The TX command requests SF7, CR4/8 and the 16-byte UTF-8 text `Hello from XIAO!`.
It sends exactly once. A `TXEND NATIVE ok` response reports local completion;
the LR2021 must independently return the same hex bytes with CRC OK.

For SendOnce, open the serial monitor and type `s`. The sketch prints local TX
status and airtime, then waits for another explicit command.

To install that example instead of SerialBench:

```sh
pio run -e xiao-send-once -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

## When a packet does not arrive

Compare frequency, bandwidth, SF, sync, header mode and CRC settings first.
Check the correct receiver RF port and both antennas. Look at TX errors and
timing counters; a reset, timeout, unsupported configuration or missing packet
is a failure, not delivery. A full CRC-valid packet with a different payload
also does not prove delivery of the requested packet.

SF7 at 406.25 and 812.5 kHz is also measured; set `bandwidthHz` and
`dacWindowSamples` to 406250/7500 or 812500/3750, and match the receiver.
The serial commands are `BW 406250` then `WIN 7500`, for example.

SF8/SF9, calibrated power and native packet reception are not
currently verified capabilities of this release. Use the default SF7 profile
for the first test. Record successes and failures when changing a parameter.
