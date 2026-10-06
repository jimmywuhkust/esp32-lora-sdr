# Two-bank DAC research

Isolated, undocumented ESP32-S3 hardware experiment; not the production library.
It reserves SRAM banks 1 and 2 before heap initialization, fills the inactive
bank, and switches the DAC grant after DONE. ROM's bank 3 is never granted.
The compact phase/LUT source stays outside those banks. An FNV-1a before/after
check detects changed source hashes; it is not a cryptographic or bytewise proof.

At gap15, a new shuffled SF7/BW203.125 matrix delivered **48/48** exact payloads
with independent LR2021 header/CRC checks: four CRs, lengths1/8/32/80, three
blocks, seed2026100719. The descriptive Wilson95% interval is92.59–100%; each
tuple has only three trials on one stationary board pair. No retries.
No late-buffer deadlines were reported; maximum fill74,228 CPU cycles versus
a98,304-cycle bank-playback budget. Measured software retriggers were90–102
cycles. These counters do **not** establish gapless analog RF output.

SF8 gave0/3 at gaps13–18 and CFO−15/+10/+14/+20/+30kHz. Related diagnostics
reuse payload sequences; do not pool them. Short SF8 payloads across four CRs
gave0/8. Starting a freshly flashed receiver atSF8 gave0/3. SF8/BW406.25 and
SF9/BW812.5 gave0/3 each. All unsuccessful trials and image/source hashes are
retained under `evaluation/data/research/ping-pong-*.json`.

The nominal250ms bound and actual elapsed-cycle abort protect the experimental
interrupt-masked section. Commands send one bounded packet; nothing transmits
at boot. Restore the production SerialBench after the experiment.

```sh
pio run -d research/ping-pong-dac
python evaluation/verify_public_receiver.py --tx-port COM3 --rx-port COM4 \
  --transport STREAM --gap 15 --lengths 1,8,32,80 --repeats 3 --seed 2026100719 \
  --tx-image research/ping-pong-dac/.pio/build/xiao-stream-research/firmware.bin \
  --tx-source research/ping-pong-dac/src --output ping-pong-matrix.json
```

The current source includes the later bandwidth diagnostic. Each raw log
records the exact source hashes/image used for that variant; rebuilding the
latest source need not reproduce an earlier binary hash. SRAM timing and
ownership research credit the0BSD esp32-sdr-trx experiments.
