# Public LR2021 receiver companion

Independent CRC and complete-payload verification using **RadioLib 7.7.0**.
This source was written for this project and does not include the privately
supplied AeroLink application or driver. It listens at 2440.125 MHz, BW203.125
kHz, SF7, explicit header, private sync 0x12, CRC required, preamble 16.

The verified AeroLink board uses ESP32-S3, 8 MB flash, a 1.8 V TCXO, and the
**HF antenna port** for 2.4 GHz. Its existing Heltec WiFi Kit32 V3 build profile
is retained here because that profile was verified on this specimen; this does
not identify every AeroLink/custom PCB as a Heltec board.

| LR2021 connection | ESP32-S3 GPIO |
|---|---:|
| NSS | 8 |
| SCK | 9 |
| MOSI | 10 |
| MISO | 11 |
| RESET | 12 |
| BUSY | 13 |
| DIO9 / IRQ | 14 |

Check your wiring and save a firmware backup before flashing a different board.
No GNSS data or autonomous beacons are used. This companion never transmits.

```sh
cd companion/lr2021
pio run
pio run -t upload --upload-port YOUR_RECEIVER_PORT
pio device monitor --port YOUR_RECEIVER_PORT --baud 115200
```

`INFO` prints startup status and readiness. A valid packet prints:

`SF 7`, `SF 8`, `SF 9` and `BW 203125`, `BW 406250`, `BW 812500` change
receiver settings explicitly. These commands acknowledge the chip result;
an accepted SF setting does not establish that the XIAO waveform interoperates.
`FREQ 2440125` selects kHz, `PRE 16` the preamble, `SYNC 18` the decimal sync
word (0x12), and `INV 0` standard IQ (`INV 1` inverted). Match both sides;
XIAO `INV 1` matches LR2021 `INV 0` because of the measured S3 convention.
Every setting enters standby, checks the chip result and restarts reception.

```text
RX status=0 header=0 crc_present=1 crc_ok=1 bytes=... rssi=... snr=... hex=...
```

Every condition must match, including exact expected hex and length. A missing
CRC is rejected even if RadioLib returns no CRC mismatch. CRC failure lines
are retained, not displayed as successful packets. The implementation polls
chip IRQ status as well as accepting a GPIO interrupt, and enters standby
before draining and restarting reception; both were necessary in initial
hardware experiments. An RX restart failure prints `RX_STOP` and halts.

Close both serial monitors before running the host verifier from the repo root:

```sh
python -m pip install pyserial==3.5
python evaluation/verify_public_receiver.py --tx-port YOUR_XIAO_PORT --rx-port YOUR_RECEIVER_PORT --smoke --repeats 3
python evaluation/verify_public_receiver.py --tx-port YOUR_XIAO_PORT --rx-port YOUR_RECEIVER_PORT --repeats 10 --output evaluation/data/my-matrix.json
```

The matrix sends 240 fresh randomized packets across CR4/5–4/8 and lengths
1,8,32,80,128,255. It sends once per trial and saves every success/failure.
The report identifies which receiver implementation produced each dataset.
