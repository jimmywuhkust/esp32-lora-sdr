# Playing-bank grant experiment

Isolated research; excluded from the Arduino library. The main library keeps
its independently verified playback-window implementation. No automatic RF TX.

The prior [LUT experiment](../full-symbol-dac-lut/) could copy fast enough but
corrupted playing SRAM. The pinned upstream DESIGN-IQ-TX ends by asking whether
revoking the SRAM grant while the CPU writes could help. This project tests that
hypothesis, with independent digital readback and then actual RF packets.

## Digital readback: grant release recovers the stored words

Each of three alternating triplets wrote the same 16,384-word pattern:

| Condition | Wrong words | Max 256-word copy | Playback elapsed cycles |
|---|---:|---:|---:|
| Idle | 0 | 1,199 | 78,152 (writer only) |
| Playing | 16,252 | 1,199 | 99,955 |
| Playing, revoke grant around CPU writes | 0 | 1,204 | 99,958 |

No RF was keyed. Engine completion was checked separately. Repeated deterministic
checks are not independent statistical trials. This proves readback recovery for
this writer on this board, not the analog waveform or uninterrupted output.
Readback image SHA256: `367829a037ca8b05b10842dd8653593acec30d37d3de4f97e611d39390fdd40d`.
That image preceded adding grant release to the actual RF fill path.

## RF validation: bounded SF7 packets now decode

After wrapping RF buffer fills in the same grant release, 256-word blocks and
64-sample margin delivered 3/3 fresh 32-byte SF7/CR4/8 smoke packets. A separate
seed2026100708 shuffled matrix then delivered **48/48**, four CRs times lengths
1/8/32/80 bytes times three fresh-payload blocks. These are exact full bytes,
explicit header, CRC present, receiver status zero and hardware CRC success.
Descriptive aggregate Wilson 95% lower bound is approximately 92.6%; each cell
has only three trials. One stationary indoor board pair, not a range experiment.

RF image SHA256: `83be4fff6085b33a7cbbeea2e642cc0911d630c6dcfe4f8fc72dad60723af255`.
This is the current source scheduling: block256/tail512/margin64/gap14. It uses
approximately 78-cycle retriggers and incurs roughly one late first block per
buffer. Its true RF coverage and spectrum have not been independently measured;
"continuous" here names the refill experiment, not proven gapless RF output.

SF8 still delivered 0/3 at gaps14,13,0,27; the latter three reuse a diagnostic
payload sequence and must not be pooled as independent randomized trials.
A separate CFO0 test also gave 0/3. One SF8 line claimed CRC success but contained
32 bytes of A5 instead of the requested random data, so it **failed** our rule.
No byte-pattern blacklist is used; legitimate A5 payloads are allowed.

Larger blocks did not help RF: block512/margin128 SF8 0/3, and block512/margin64
SF7 0/3 and SF8 0/3. They reduced late-block counts to one per packet, but the
first variant increased maximum seams to354 cycles. Readback/timing improvements
alone cannot justify promoting an experimental path.

The code masks only bank bits0..3 during CPU writes and restores the playing
grant. Bank3 must never be granted: it overlaps ROM working data. Nominal airtime
is capped at250ms, plus a real elapsed-cycle abort around229ms; all example
packets are bounded. Restore the main SerialBench after research.

```sh
pio run -d research/playing-bank-grant
python evaluation/verify_playing_bank.py --port COM3 --grant \
  --image research/playing-bank-grant/.pio/build/xiao-stream-research/firmware.bin \
  --output readback.json
python evaluation/verify_public_receiver.py --tx-port COM3 --rx-port COM4 \
  --transport STREAM --gap 14 --lengths 1,8,32,80 --repeats 3 --seed 2026100708 \
  --tx-image research/playing-bank-grant/.pio/build/xiao-stream-research/firmware.bin \
  --tx-source research/playing-bank-grant/src --output matrix.json
```

Raw files: `evaluation/data/research/playing-bank-grant.json` and
`evaluation/data/research/grant-stream-*.json`. Every RF log preserves the source
hashes of its actual variant; the 512-word experiments changed only the scheduling
constants from this source. RF image hashes are recorded per log. CPU schedules
and the original corruption hypothesis credit the 0BSD esp32-sdr-trx research.
