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
- **jkadbear LoRaPHY**, MIT, copyright 2020–2022 jkadbear: reference for native
  synchronization, folded FFT demodulation and packet processing. The native
  code is expressed in C++; its math was compared with the installed
  `lora-phy 0.3.0` Python translation and actual RF recordings.
  https://github.com/jkadbear/LoRaPHY
  The Python package's referenced repository was unavailable during this
  review, so no additional unverified license claim is made for that repository.
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

MIT notice for the LoRaPHY reference:

Copyright (C) 2020-2022 jkadbear, jkadbear@gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
IN THE SOFTWARE.
