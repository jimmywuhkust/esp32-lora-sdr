# Source and dependency credits

The Arduino transmitter and host adapters are GPL-3.0-only under `LICENSE`.
Experimental firmware is kept separate from the supported library sources.

- `firmware/iq-capture`: GPL-3.0 ESPARGOS/esp-sdr source with local changes.
  Its `provenance.json` records origin, pin and the pre-edit copied-file hashes.
  The core1 startup notes its0BSD eSpDR startup reference in the source comment.
- DAC register/timing research: Jochen Hammes'0BSD esp32-sdr-trx, pinned and
  linked in `docs/prior-art.md`; research source retains attribution comments.
- ESP-IDF and ESP-DSP: fetched public Espressif dependencies. Their original
  license files apply to SDK/component contents; they are not relicensed here.
- RadioLib7.7.0: public MIT dependency, fetched by PlatformIO. The companion
  does not bundle the private AeroLink implementation.
- lora-phy0.3.0 by Zhang Maiyun: public MIT Python dependency translated from
  jkadbear/LoRaPHY. The host retains its dependency rather than copying it.

Firmware readbacks containing NVS, account credentials and private AeroLink
source archives are excluded. Raw bench payloads are synthetic/random test
bytes, with genuine independent hardware or captured-IQ results.
