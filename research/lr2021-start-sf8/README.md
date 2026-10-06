# Historical LR2021 TX-completion diagnosis

This isolated RadioLib 7.7.0 fixture investigated why blocking TX reported
success while XIAO recorded no usable packet. Use the maintained
[public companion](../../companion/lr2021/README.md), `aerolink-hf-tx`, instead.

This research snapshot enables GODMODE for chip-error diagnostics. Its manual
TX is limited to SF7–9, CR4/5–4/8, 1–32 bytes, 2440.125 MHz, BW203.125 kHz,
preamble 16, sync0x12, explicit header and CRC. Requested HF power is −12 dBm;
actual radiated power is uncalibrated. It does not key RF automatically at boot.

GPIO14 read high while chip IRQ was zero on this board. Blocking TX depended
on that GPIO and ended the transmission prematurely in our initial fixture.
Two initial batches were 0/9. A FIFO-clear + actual-TX_DONE-poll variant was
9/9; a separate IRQ-only variant retaining original FIFO behavior was also
9/9 with new payloads. All required independent XIAO IQ, blind PC decoding,
exact full payload and CRC. Intermediate incomplete/serial-truncated batches
remain failures in [the raw data](../../evaluation/data/research).

The source evolved during diagnosis. Each report records its actual source
and image hashes; this snapshot is not every earlier variant. The observation
does not establish a general RadioLib bug or a specific wiring/silicon fault.
The maintained companion uses only public APIs, without GODMODE.
