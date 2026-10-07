# Standalone XIAO IQ capture firmware

**Current source also includes native packet RX/TX in the PSRAM build.**
It calls the same `LoRaRadio` C++ library as the standalone native application.
Use [the native guide](../../docs/native-guide.md) for on-device packets.
`prebuilt/default` and `prebuilt/psram` are the earlier, frozen IQ-to-PC
artifacts documented below. `prebuilt/native` is the separately identified
native RX/TX bench image; its manifest records generated images and hashes.

This ESP-IDF application captures the XIAO ESP32-S3's internal 2.4 GHz RF and
sends CRC32-protected IQS1 frames over native USB Serial/JTAG. The included PC
decoder extracts full LoRa headers, payloads and CRC. **It does not implement
native Arduino full-packet reception.** Switch between this capture image and
the Arduino transmitter with a complete firmware upload, including bootloader
and partition table; their SDK versions differ.

Source derives from GPL-3.0 ESPARGOS/esp-sdr plus our bounded capture/transport
changes. `provenance.json` records the upstream commit and original copied-file
hashes. The archived IQ-only images had their TX command removed. No
private AeroLink source, calibration/NVS backup or GNSS data is included.

## Pinned toolchain

- ESP-IDF development snapshot `25fe69f946311abdaf9ad56591f25fedbc20ac98`
  from [Espressif](https://github.com/espressif/esp-idf/tree/25fe69f946311abdaf9ad56591f25fedbc20ac98).
  Tested locally with Python3.13 and the SDK's Xtensa16.1.0 toolchain.
- ESP-DSP `a53a0756833c045311ea1d79a2badf495cdfde4c`, fetched by
  `python setup_deps.py`; its Apache-2.0 source/license stays in the dependency.
- Target ESP32-S3, 8MB flash, native USB Serial/JTAG, 240MHz. PSRAM is not
  required by the default image. Other chips/SDK versions are unverified.

Install and export that exact ESP-IDF version using its official setup scripts.
On Windows use an ASCII-only checkout path, e.g. `C:\lora-sdr`; our local build
used an ASCII staging directory because the shared workspace has Chinese text.
Inside this directory, with the ESP-IDF environment active:

```sh
python setup_deps.py
idf.py -DIDF_TARGET=esp32s3 build
idf.py -p YOUR_XIAO_PORT flash
```

The standard flasher writes only generated bootloader, partition table and
application binaries. **Do not distribute a readback of the NVS/boot region.**
Save any firmware you need before changing applications. After startup, `INFO`
returns `S3SDR 9 iq-capture 16380`. No packet is transmitted at boot.

## Live complete packets

Flash the LR2021 with the optional `aerolink-hf-tx` environment described in
[the public companion](../../companion/lr2021/README.md). Close serial monitors.
Then run the [live host workflow](../../host/README.md) from the repository root.
It waits for the first validated IQ frame before issuing each RF TX command;
expected bytes are compared only after decoding.

The measured setting is BW203.125kHz, SF7–9, sync0x12, explicit header/CRC,
with 250kcomplex samples/s, 4-bit components and hardware AGC. The 108-case
default-image matrix decoded104 packets: SF7 33/36, SF8 35/36, SF9 36/36.
The four failures are retained. These are one stationary pair's bench data,
not distance or sensitivity measurements.

500ms windows in that matrix retained99.18–100% of the decimated IQ; 74 windows
reported output-frame loss. The decoder splits gaps rather than filling them.
Sample retention within a window and elapsed RF duty over an entire session
are different. This firmware does not provide continuous4MS/s IQ over USB.
Bounded ring capture masks interrupts; watchdogs are disabled in this standalone
image, unlike the Arduino transmitter. Keep acquisition windows bounded.

## PSRAM queue experiment

The optional `sdkconfig.psram` + `-DIQ_CAPTURE_PSRAM_QUEUE=ON` build uses a
256 KiB output queue on a **verified XIAO S3 with 8 MB OPI PSRAM**. It is isolated
from the default SRAM queue. One fresh 36-case matrix decoded 34 packets and
retained all output IQ in every 500 ms window. A longer batch decoded 103/106
completed windows, then aborted on the 107th ring boundary. Two earlier PSRAM
batches also aborted. These failures remain in the report. The optional mode
is experimental; it does not establish uninterrupted capture or faster USB.
Use a distinct build/configuration directory:

```sh
idf.py -B build-psram -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG=build-psram/sdkconfig \
  "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.psram" \
  -DIQ_CAPTURE_PSRAM_QUEUE=ON build
```

中文：这份固件让 XIAO 采集真实 IQ，电脑解出完整 LoRa 包。默认不需要PSRAM；
它与 Arduino 发射固件是两个不同应用，切换必须完整刷写。原生 Arduino 收包、
全时4MS/s USB、校准功率和远距离能力仍未实现。

## Flash without compiling the SDK

Install `esptool==5.4.0` in your Python environment. The included prebuilt
images are generated bootloaders, partition tables and applications, with
SHA256 and source/dependency pins; no readback or NVS image is included.
Use only the verified 8 MB flash XIAO S3. The `psram` variant additionally
requires 8 MB OPI PSRAM. Close every program using that USB port, then run:

```sh
python flash_prebuilt.py --variant default --check-only
python flash_prebuilt.py --variant default --port YOUR_XIAO_PORT
```

For the new **8 MB OPI PSRAM native RX/TX bench** use `--variant native`.
Its commands include `TX SF CR HEX`, `RXPACK SF WINDOW_MS`, and settings
such as `LSET POWER 75`, `LSET BW 203125`, `LSET FREQ 2440125`.
The application performs all packet processing on the ESP32 and does not
send any test packet automatically at boot.

For the optional PSRAM experiment substitute `--variant psram`. The default
final image passed a fresh SF7/8/9 9/9 live smoke after a new build and flash;
all nine windows still reported output drops. The older 104/108 matrix used
the earlier default image; do not assign it to a changed binary. Compare
manifest hashes with the report. Re-upload the complete Arduino transmitter
using PlatformIO when switching back to XIAO TX.
