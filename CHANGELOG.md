# Changelog

## 0.1.0 — experimental, 2026-10-07

- Native Arduino/PlatformIO ESP32-S3 complete-packet LoRa transmitter, with
  portable PHY encoding and independent LR2021 hardware CRC evidence.
- Verified SF7, CR4/5–4/8 and lengths 1–255. Separate 203.125 / 406.25 / 812.5
  kHz bench matrices and firmware hashes accompany the results.
- Bounded, explicit-command SendOnce and SerialBench examples; no boot beacon.
- Public RadioLib 7.7.0 LR2021 receiver companion and reproducible USB verifier.
- English and Chinese guides, scientific figures with confidence intervals,
  retained failures and credited prior work.
- Optional, experimental analog PBUS gain control with readback, restoration
  and non-raising guards; default 0 leaves the measured RF defaults intact.
- Channel/preamble/sync/IQ-polarity selection with a 142/144 hardware settings
  matrix and 24/24 guards. All failed payloads remain in the data.
- PC decoder companion and real reverse-link IQ fixture, with CRC/gap/EOF
  regressions; native Arduino packet RX remains unsupported.
- Prior-art review including Wi-Lo/WiRa/Wi-Lo++ and reproduced playing-bank
  SRAM corruption; the failed fast streamer remains isolated research.

This is not a complete SX1262/SX1280 replacement. Reliable SF8/SF9 transmission,
gap-free continuous waveforms, calibrated power, weak-signal SF recovery and
native Arduino packet reception remain unachieved. The experimental continuous
streamer is isolated from the working library.
