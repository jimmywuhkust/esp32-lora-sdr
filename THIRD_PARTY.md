# Third-party work

- **ESPARGOS esp-sdr**, GPLv3: foundation for the separate capture firmware
  and the investigation of the S3 RF chain. https://github.com/ESPARGOS/esp-sdr
- **Jochen Hammes esp32-sdr-trx**, 0BSD, pinned research commit
  `6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0`: PHY reference, register behavior
  and documented successful/failed RF experiments. The native encoder was
  independently expressed in C++ and cross-checked against that Python PHY.
  The bounded PBUS writer also adapts its `firmware/src/radio.c` register
  transaction and retains non-raising, readback and restoration checks.
  https://github.com/jochenhammes/esp32-sdr-trx
- **lora-phy 0.3.0**, upstream project by the package authors: host PHY coding,
  CRC reference and separate PC decoder. https://pypi.org/project/lora-phy/
- **CNLohr LoLRa**: prior art. RF implementations from its mixed-license
  repository are not bundled. https://github.com/cnlohr/lolra
- **Espressif ESP-IDF / Arduino core**: SDK and prebuilt PHY routines installed
  by the build system; their upstream license terms apply to SDK components.
- **PlatformIO** supplies the pinned build environment. It is not part of this
  source distribution.
- **RadioLib 7.7.0**, MIT: public LR2021 companion dependency installed by
  PlatformIO. https://github.com/jgromes/RadioLib

No private AeroLink source archive, GNSS data, account credentials or tool
environment is included. Hardware results from the private bench receiver
must not be confused with a tested public receiver firmware release.

0BSD license notice for the esp32-sdr-trx reference:

Permission to use, copy, modify, and/or distribute this software for any purpose
with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
