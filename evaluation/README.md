# Reproduce the bench measurements

## Arduino on-device RX/TX and two ESP32s

Build and flash the [ArduinoDuplex profiles](../docs/arduino-rx.md). For the
six directed links, both ESP32s need `xiao-arduino-bench`; the LR2021 uses its
public TX/RX companion. The host schedules real RF and checks complete bytes
after MCU decoding. No expected payload is supplied to either decoder.

```sh
python evaluation/verify_three_radios.py --a ESP32_A_PORT --b ESP32_B_PORT --lr LR_PORT --image BENCH_BIN --lr-image LR_BIN --output NEW_TRIAD_JSON
```

For autonomous ping/pong, flash `xiao-arduino-pong` first and
`xiao-arduino-ping` last. Start this read-only observer within the initiator's
10-second startup delay. It writes zero serial bytes to either ESP32. The
optional LR2021 independently observes packets; its settings are host-written.

```sh
python evaluation/verify_arduino_ping_pong.py --ping INITIATOR_PORT --pong RESPONDER_PORT --lr LR_PORT --ping-image PING_BIN --pong-image PONG_BIN --output NEW_PAIR_JSON
```

To test the Arduino echo application against the LR2021, use
`verify_native_echo.py --application arduino` and pass the actual echo image.
Read [the Arduino report](../docs/arduino-report.md) for successes, misses,
firmware identities, explicit RF repeats and finite-window limitations.

For a short SF7–12 RX check with the same four rejection gates:

```sh
python evaluation/verify_native_rx.py --profile smoke --xiao ESP32_PORT --lr2021 LR_PORT --image BENCH_BIN --lr-image LR_BIN --output NEW_SMOKE_JSON
python evaluation/verify_native_tx.py --xiao ESP32_PORT --lr2021 LR_PORT --image BENCH_BIN --lr-image LR_BIN --output NEW_TX_JSON
```

The TX fixture checks the raw LR2021 IRQ and entire payload. Both fixtures
use the serial-bench application, with encoding/decoding on the ESP32.
For Windows ASCII staging builds, record all five images and refuse source
mismatches with:

```sh
python evaluation/record_arduino_build.py --staging STAGED_REPOSITORY --output NEW_BUILD_JSON
```

The included final datasets are `arduino-three-radios-final.json`,
`arduino-tx-final.json`, `arduino-rx-smoke-final.json` and
`arduino-echo-final.json`. Intermediate, unsuccessful and interrupted runs
are retained separately. Counts from different builds are not pooled.

For full on-device RX/TX, first follow the [native guide](../docs/native-guide.md).
From the repository root, with both serial monitors closed:

```sh
python evaluation/verify_native_echo.py --xiao YOUR_XIAO_PORT --lr2021 YOUR_LR_PORT --output echo-result.json
python evaluation/verify_native_rx.py --xiao YOUR_XIAO_PORT --lr2021 YOUR_LR_PORT --output rx-result.json
python evaluation/verify_native_levels.py --xiao YOUR_XIAO_PORT --lr2021 YOUR_LR_PORT --output levels-result.json
```

The echo fixture only reads the XIAO console: its own application receives,
decodes, validates and replies. The RX matrix and amplitude sweep require the
separate `prebuilt/native` serial bench, not the autonomous echo application.
All three preserve raw replies and use no PC packet decoder. Supply image
paths where supported to identify the exact last-flashed build; hashes of a
local file are not an automatic device readback. See [native results](../docs/native-report.md).

The echo fixture records `strictRfTxPassed` from the independent CRC/header
readout and its following IRQ, separately from the older `txPassed` criterion
that also requires the local ACK console label. An interrupted current case
and error are retained. The new `native-echo-library-settings.json` verifies
the standalone profile API; its eight attempts are separate from the older
eight-bit echo dataset.

The older Arduino TX experiments follow below.

The independent receiver is a real LR2021. Local TX completion, a fresh
CRC-present/CRC-valid RX result, matching length and exact payload bytes are
all required. A spectrum peak alone does not count. No RF retries are hidden.

1. Build and upload `xiao-s3` from the repository root.
2. Build and upload the [public receiver](../companion/lr2021/README.md).
3. Close serial monitors and disconnect the local web controller.
4. Install `pyserial==3.5`, substitute the two ports, and run:

```sh
python verify_public_receiver.py --tx-port YOUR_XIAO_PORT --rx-port YOUR_LR2021_PORT --smoke --repeats 3 --output data/my-smoke.json
python verify_public_receiver.py --tx-port YOUR_XIAO_PORT --rx-port YOUR_LR2021_PORT --repeats 10 --seed 20261007 --output data/my-matrix.json
```

Run those commands from `evaluation/`. The full matrix sends 240 packets:
four coding rates × six lengths × ten fresh-payload repetitions, with shuffled
configuration order in every block. Give each run a new output name. Every
trial is checkpointed; a lost TX transaction or stopped receiver aborts the
run and retains the failure. A missing RF packet is recorded and the run
continues. Use `--sender sendonce --repeats 3` after uploading `xiao-send-once`
to verify that beginner example.

The harness records the SHA256 of local last-flashed images when present.
This is not an automatic device readback. Flash the corresponding build and
retain esptool's write-verification result. `--tx-image` and `--tx-source`
identify separately built research firmware. Do not label a research image
with a production binary hash.

## Included datasets

The full native MCU sender can be checked separately from stock Arduino.
Flash `prebuilt/native` or build `NativeDuplex/xiao-bench`, disconnect the UI,
and give each run a new output path. If using a different build, pass its
actual last-flashed `--image` and `--lr-image` paths:

```sh
python evaluation/verify_native_tx.py --xiao YOUR_XIAO_PORT --lr2021 YOUR_LR2021_PORT --output evaluation/data/my-native-tx.json
python evaluation/plot_native_tx.py
```

Run these from the repository root. The fixture sends one fresh SF7 packet
for each CR4/5–4/8 × length1/8/32/80/255 combination and starts a fresh LR2021
RX interval before each attempt. It records the previous IRQ state and keeps
the same strict CRC gate. The plotting script uses the included fixed dataset;
it does not automatically replace it with a new measurement.

| File | Measurement |
|---|---|
| `native-matrix-basic.json` | Original native SF7 matrix: 237/240, lengths 1–250 |
| `public-receiver.json` | Public receiver matrix: 239/240, lengths 1–255 |
| `sendonce.json` | Beginner sketch: 3/3 independently received complete CRC packets |
| `native-encoder.json` | 264 on-device symbol-encoding checks; not RF tests |
| `native-amplitude-sf.json` | 180 SF/amplitude trials; only SF7 delivered packets |
| `public-receiver-usb-desync.json` | Retained broken transport run; not an RF sensitivity estimate |
| `analog-gain-sf7-sweep.json` | 70/70 at seven shuffled PBUS codes, constant DAC amplitude, verified readbacks |
| `analog-gain1-matrix.json` | Separate lowest-code research matrix: 71/72 across four CRs and 1–255 bytes |
| `phy-settings-matrix.json` | 142/144 across channel, preamble, sync and IQ polarity; two corrupt CRC frames retained |
| `phy-config-guards.json` | 24/24 guard checks on the latest settings-capable bench image |
| `research/stream-lut-playing-bank.json` | Digital SRAM paired idle/playing readbacks; no RF keying |

The older private-application datasets are retained as historical evidence;
the public receiver is the reproducible path. Different builds, payload sets
and receiver implementations must not be pooled into a controlled comparison.

## Regenerate scientific figures

```sh
python -m pip install numpy==2.5.3 matplotlib==3.11.2
python plot_results.py
python plot_settings.py
```

The script reads raw JSON and exports SVG, PNG and PDF to `docs/assets/`.
Wilson 95% intervals describe these finite bench samples. Raw DAC amplitude
is not dBm, successful-packet RSSI is conditional on reception, and theoretical
airtime is not a measured RF trace. See the [report](../docs/test-report.md)
for confounders, failures and the exact firmware hashes.

The additional 144-trial settings matrix uses both sides' explicit setting
commands and restores the defaults afterward:

```sh
python verify_phy_settings.py --tx-port YOUR_XIAO_PORT --rx-port YOUR_LR2021_PORT --repeats 3 --output data/my-settings.json
```

It tests 48 combinations at fixed SF7/BW203.125/CR4/8 and 32 bytes, not every
cross-product of every field. The interrupted pre-checkpoint-fix dataset is
retained separately; do not silently combine it with the completed matrix.
The [host decoder tests](../host/README.md) replay saved RF and synthetic
signals; their pass count is not a live packet-delivery measurement.

Reverse live tests: [host workflow](../host/README.md). The optional capture queue studies and all aborted batches have [raw reports](data/research/capture-retention-summary.json) and an [actual IQ archive](data/captured-iq.zip). Regenerate the new figures with `python plot_reverse.py`, `python plot_grant.py`, and `python plot_capture_retention.py`.
