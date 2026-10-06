# Unsuccessful full-symbol DAC experiment

These files are a separate research snapshot, not the library backend in `src/`.
Do not replace the working backend with them for a beginner demonstration.

The attempt expands a compact LoRa phase ring through a 256-entry I/Q table.
Following the 0BSD esp32-sdr-trx E1 scheduling experiment, it writes blocks only
after the playback engine has read them, immediately re-triggers at DONE, and
fills the last 512 words after that trigger. It measures late blocks and actual
CPU-cycle gaps. Predicted gaps are skipped in the sample cursor.

Initial tests: **0/3 at SF7**, **0/3 at SF8**, CR4/8, changing 17-byte payloads.
GAP14 and GAP27 both failed at SF7. An O3 follow-up still failed0/3. The copy
budget was close to or above the engine's six CPU cycles per sample, with
occasional interrupt-service gaps. No CRC-valid LR2021 proof was obtained.
These are failed experiments, not evidence of continuous LoRa transmission.

The working windowed backend and its237/240 matrix remain in the main library.
Full-symbol streaming, shorter gaps and higher-SF interoperability need further
investigation, waveform observation and hardware testing.
