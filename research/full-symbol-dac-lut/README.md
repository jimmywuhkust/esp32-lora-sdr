# LUT stream: fast copy, failed playing-bank integrity

Isolated research project, excluded from the Arduino library. It is **not a
working continuous LoRa transmitter**. No automatic transmission occurs.

The writer uses a pre-scaled 16-bit phase ring and two internal-RAM 256-entry
I/Q lookup tables. An Xtensa assembly loop copies four samples per iteration.
`LUTTEST` checks 1,024 combinations of source offset and length, including
destination canaries, without RF. Its measured worst 256-sample copy was
1,186 cycles; the 40 MS/s reader budget is 1,536 cycles at 240 MHz.

Nevertheless SF7/BW203.125 kHz streaming delivered **0/3** exact CRC packets
at each tested seam compensation: 0, 13, 14 and 27 skipped samples. These
small sequential diagnostics reused a payload sequence; do not pool them as
independent randomized trials. The logs identify the tested source/image.

Following the upstream [E1 failure analysis](https://github.com/jochenhammes/esp32-sdr-trx/blob/6de35a5138c8f6d7bf6af2b6c8dd0c99342e72f0/docs/research/DESIGN-IQ-TX.md),
we added `BANKIDLE` and `BANKPLAY`. They do not key the RF chain. Identical
16,384-word patterns are written with the same loop; the playing case waits
until each block is behind the reader. SRAM is compared after playback ends.

Three alternating paired checks gave:

| Engine state | Wrong words / 16,384 | Maximum 256-word copy |
|---|---:|---:|
| Idle, each of three checks | 0 | 1,187–1,188 CPU cycles |
| Playing, each of three checks | 16,201 | 1,187 CPU cycles |

This directly demonstrates that this writer fails to preserve SRAM contents
during playback on this XIAO, even when its copy speed meets the budget. It
does not establish a silicon-level cause, a universal error fraction, or the
exact emitted RF waveform. The upstream used a different writer/pattern and
reported different corruption counts; its numbers are not ours.

Idle checks after playing checks returned zero wrong words, so the test does
not imply permanent SRAM damage. `BANK ok=1` means the engine completed; only
`bad=0` means the readback matched. A completed engine is not successful RF.

The stream includes a real elapsed-cycle abort of approximately 229 ms, in
addition to the nominal 250 ms airtime guard. The working library continues
to modify RF SRAM only between playback windows.

```sh
pio run -d research/full-symbol-dac-lut
python evaluation/verify_playing_bank.py --port COM3 \
  --image research/full-symbol-dac-lut/.pio/build/xiao-stream-research/firmware.bin \
  --output evaluation/data/research/stream-lut-playing-bank.json
```

Raw data: `evaluation/data/research/stream-lut-*.json`. Initial RF image SHA256:
`fa14408fde215a6fbe015eef39113c62486736c83a5108f21cda44e7cb9e7838`.
Later readback image SHA256:
`33a60a34db837392e6ea476b5bd937eaefc8d0da206c4cf9380b50c0500eb15d`.
The source includes the later readback commands; earlier logs retain their
own source hashes. Scheduling was adapted from the credited 0BSD experiment.
