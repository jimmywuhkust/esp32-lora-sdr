# Send and receive with Arduino

Start with the supplied PlatformIO project. Upload the receiver, send a
packet from another radio, then replace the example with your own program.

Use PlatformIO for reception and transmission. Arduino IDE can run
[SendOnce.ino](quick-start.md) for transmission only; RX is not supported
in the stock Arduino IDE build.

## 1. Get the project

You need a XIAO ESP32-S3 with 8 MB flash and 8 MB OPI PSRAM, its 2.4 GHz
antenna, a USB data cable, and another radio to send packets. The peer can be
another ESP32 running this project or a compatible 2.4 GHz LoRa transceiver.

Install Python, then download and extract the repository ZIP. You can also
clone it using your GitHub account. On Windows use a path such as `C:\lora-sdr`.
Open a terminal in the repository directory and run:

```sh
python -m pip install platformio==6.1.19
python examples/NativeDuplex/setup_deps.py
```

The setup command downloads the DSP dependency. The first build downloads
the compiler and Arduino packages.

## 2. Upload the receiver

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx
pio device list
```

Find your board's port in the list. Replace `YOUR_PORT` below with that port,
for example `COM3` on Windows. Close any serial monitor using it, then run:

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-rx -t upload --upload-port YOUR_PORT
pio device monitor --port YOUR_PORT --baud 115200
```

On startup, look for `ARDUINO_BEGIN status=ok`. The program then prints
`LISTEN arduino sf=7 window=500` while waiting for packets.

## 3. Send a packet to it

Set the other radio to:

| Setting | Value |
|---|---|
| Frequency | 2440.125 MHz |
| Bandwidth | 203.125 kHz |
| SF | 7 |
| Coding rate | 4/8 |
| Preamble | 16 symbols |
| Sync word | `0x12` |
| Header | Explicit |
| Payload CRC | On |
| IQ | Standard |

Send a short packet. The receiver prints `ARDUINO_RX crc_ok=1`, its length
and the payload in hex. It prints capture diagnostics on the next line.
The [LR2021 companion](../companion/lr2021/README.md) provides a tested peer.

To have this board reply, upload `xiao-arduino-echo` instead:

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-echo -t upload --upload-port YOUR_PORT
```

It sends `ACK:` followed by the received bytes. Check those bytes and CRC on
the other radio. The reply accepts inputs up to 251 bytes so the prefix fits
in a 255-byte packet. After upload, the board can run from a USB power supply;
serial output is optional.

## Try two ESP32s

Keep the bundled example unchanged for this test. Connect both boards and
use `pio device list` to find their ports. Upload the responder first:

```sh
pio run -d examples/ArduinoDuplex -e xiao-arduino-pong -t upload --upload-port RESPONDER_PORT
pio run -d examples/ArduinoDuplex -e xiao-arduino-ping -t upload --upload-port INITIATOR_PORT
```

The initiator waits ten seconds, then sends four distinct requests. The
responder decodes each request and sends its contents back with `PONG` in
place of `PING`. Open the initiator's monitor:

```sh
pio device monitor --port INITIATOR_PORT --baud 115200
```

`PING_RESULT ... crc_exact_pong=1` means it received the complete matching
reply with a valid CRC. `PING_FINISHED` gives the number of successful
exchanges. Power-cycle the initiator to run another four-request session.
You can swap the two programs to test the opposite direction.

Each request and reply is sent eight times because reception has blind
intervals. The example logs every copy and stops after four requests. Our
[two measured sessions](arduino-report.md) each completed three of four
exchanges; do not expect every request to arrive.

## Write your own program

Replace [`main/main.cpp`](../examples/ArduinoDuplex/main/main.cpp) with this
receiver. Keep the rest of the project files, and build `xiao-arduino-rx`
using the commands above.

```cpp
#include <Arduino.h>
#include <LoRaRadio.h>
using namespace lora_sdr;

LoRaRadio radio;
bool ready = false;

size_t getArduinoLoopTaskStackSize() { return 8192; }

void setup() {
  Serial.begin(115200);
  delay(1000);

  LoRaSettings settings;
  settings.frequencyMHz = 2440.125;
  settings.bandwidthKHz = 203.125;
  settings.spreadingFactor = 7;
  settings.codingRate = 8;              // 4/8
  settings.transmitPowerPercent = 75;   // relative amplitude, not dBm

  Error status = radio.begin(settings);
  ready = status == Error::Ok;
  Serial.printf("begin: %s\n", errorName(status));
}

void loop() {
  if (!ready) { delay(1000); return; }

  RxPacket packet;
  Error status = radio.receive(packet, 500);
  if (status == Error::Ok) {
    Serial.printf("RX %u bytes: ", unsigned(packet.length));
    for (size_t i = 0; i < packet.length; ++i)
      Serial.printf("%02x", packet.payload[i]);
    Serial.println();
  } else if (status != Error::ReceiveTimeout) {
    Serial.printf("receive: %s\n", errorName(status));
  }
  delay(10);
}
```

To send text from your program, call this after `begin()` succeeds:

```cpp
Error status = radio.transmit("Hello from ESP32!");
```

For binary data, use `radio.transmit(bytes, length)`. Check the returned
status, then confirm delivery on the peer. `receive()` returns complete
payload bytes after CRC validation; it captures first and decodes afterward.
See [the API](api.md) for settings, error codes and diagnostics.

## If it does not work

- If upload cannot open the port, close other monitors and the web bench.
- If startup does not report `ok`, check the error and that the board has
  8 MB OPI PSRAM. Use the supplied project settings.
- If it listens but receives nothing, check both antennas and every peer
  setting in the table. Start with a short SF7 packet at CR4/8.
- A successful local TX does not prove reception. Check the complete bytes
  and CRC on the other board.

Reception has gaps while decoding or transmitting. SF7–12 RX is supported
at 203.125 kHz; high SF decoding can take seconds. SF7 is the demonstrated TX
setting. SF8/9 TX is unreliable; SF10–12 TX is unsupported. Output power is
not calibrated in dBm. [Measurements](arduino-report.md) include the misses
and one intermediate CRC-valid output whose bytes were incorrect.

## Build details

The project builds Arduino 2.0.17 with ESP-IDF 4.4.7. RX needs reserved RF
memory, OPI PSRAM and a dedicated acquisition core; an installed Arduino
library cannot change the stock core's build configuration.

The supplied files configure core 0 for Arduino and core 1 for acquisition,
reserve RF SRAM, disable watchdogs and set an 8 KiB loop stack. Keep those
files and the stack override when adapting the example. Keep Wi-Fi/BLE off,
and do not create tasks on core 1. PlatformIO's generic variant warning is
expected; the project selects `XIAO_ESP32S3` in its SDK configuration.

`xiao-arduino-bench` is a separate serial-command test program. Use it only
for the [measurement fixtures](../evaluation/README.md) or optional web bench.
It is not needed by an application that calls `LoRaRadio`.

For the pair example's timing: each copy has a 350 ms idle delay, plus
waveform generation, airtime and a 200 ms USB logging delay. The responder
waits 6.5 seconds before replying. The initiator listens in 500 ms windows
for up to 18 seconds and waits 5.5 seconds between requests.
