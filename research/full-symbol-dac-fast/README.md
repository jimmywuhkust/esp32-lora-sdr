# Failed full-symbol streaming experiments

This separate PlatformIO project is research, excluded from the library build.
It does not provide a verified continuous LoRa transmitter.

Following the credited 0BSD reader/writer experiment in esp32-sdr-trx, it writes
behind the RF SRAM reader and retriggers each 16,384-sample buffer. A 40 MS/s
reader leaves only six CPU cycles per sample at 240 MHz. Our waveform cursor
also has to handle symbol boundaries, phase rotations and down-chirps.

The retained logs in `evaluation/data/research/` include:

- Internal word ring with calculated down-chirps: 0/3 at gap compensation 14,
  and 0/3 at 27. Worst measured copy was about 4,112 cycles per 256 words.
- Removing periodic interrupt service, with a nominal 250 ms airtime cap:
  still 0/3; large gaps persisted, isolating copying as a separate bottleneck.
- Unrolling the down-chirp conjugation: 0/3; maximum copy fell to 2,505 cycles,
  still above the 1,536-cycle reader budget. Maximum gap was about 46,407 cycles.
- Precomputed up/down words in PSRAM: SF7 0/3, over 10,000 late blocks per
  packet; SF8 aborted after an interrupt-watchdog reset. Nominal airtime did
  not bound the duration of an overrunning stream.

The latest source adds an actual elapsed-cycle abort at approximately 229 ms,
in addition to its nominal airtime guard. That guard was added after the saved
watchdog failure. Source/image hashes in each log identify the tested version.
Do not silently treat subsequent source edits as hardware-verified.

```sh
pio run -d research/full-symbol-dac-fast
```

No automatic TX occurs at boot. This build is intentionally separate from the
windowed-DAC library that delivered full CRC-valid SF7 packets.
