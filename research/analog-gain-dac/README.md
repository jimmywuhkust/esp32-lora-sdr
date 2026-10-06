# Analog gain research — isolated from the supported library

This build keeps DAC amplitude at 150 while changing PBUS registers (5,1) and
(5,3). The codes are undocumented, uncalibrated and not necessarily monotonic.
They are not dBm, and a received packet's RSSI is not a transmit-power meter.

The [upstream IQ TX experiment](https://github.com/jochenhammes/esp32-sdr-trx/blob/research/iq-tx/docs/research/IQ-TX-PHASE-A.md)
motivated the register choice and bounded PBUS write. Its reported keyed
default was 119. On this Arduino 2.0.17 XIAO build both defaults read **63**.
The first request for 119 was rejected before chirp playback. Never increase
the target above either actual keyed readback. Each transmission restores
both original registers and PBUS ownership before returning.

`PA 1..119` accepts a research request; transmit independently checks it
against the keyed default. The default request is 63. The evaluation sweep
limits requests to 1..63. A local register or playback error aborts evaluation,
rather than being counted as an RF sensitivity failure.

The initial 63 smoke attempt received one corrupt frame and one exact frame,
then aborted when a long completion line was fragmented. A shorter completion
line (`a=before1,before3 b=after1,after3`) passed three fresh packets. The
initial firmware used default 119 and printed buffer/copy/PA fields too; its
exact image and source hashes are retained in the failed-run JSON. Successful
completion lines now fit below 64 bytes for the 32-byte smoke profile.

Build with pinned PlatformIO dependencies:

```sh
pio run -d research/analog-gain-dac
```

After explicitly flashing this research image and the public receiver, run
from the repository root with all serial controllers disconnected:

```sh
python evaluation/verify_public_receiver.py --tx-port COM3 --rx-port COM4 \
  --smoke --repeats 10 --analog-gain-sweep 1,3,8,16,32,48,63 \
  --tx-image research/analog-gain-dac/.pio/build/xiao-stream-research/firmware.bin \
  --tx-source research/analog-gain-dac/src --output evaluation/data/analog-gain-sf7-sweep.json
```

The seven settings are shuffled independently in every block; every payload
is fresh. Acceptance requires successful local completion, both gain
readbacks, a fresh independent hardware CRC and exact full payload bytes.
No RF retries are made. This is one stationary bench, without a calibrated
attenuator, spectrum mask measurement or additional specimens.
