# PC packet decoder and real RF recording

This companion decodes complete LoRa bytes and checks payload CRC from
**XIAO-captured I/Q on a PC**. It is not native Arduino packet reception.
The transmitter library and this offline receiver workflow are separate.

## Try it without hardware

Use Python 3.13 with the pinned dependencies. From the repository root:

```sh
python -m pip install -r host/requirements.txt
python host/decode_recording.py host/samples/lr2021-sf7-window.iqs
python host/decode_recording.py host/samples/lr2021-sf8-window.iqs --sf 8
python host/decode_recording.py host/samples/lr2021-sf9-window.iqs --sf 9
python -m unittest discover -s host -p "test_*.py"
```

The included 90,014-byte recording contains a real 23-byte transmission:
`XIAO decodes full LoRa!`, hex
`5849414f206465636f6465732066756c6c204c6f526121`, payload CRC `fc8a`.
An LR2021 sent it over the air; the XIAO captured the signal. The decoder
does not receive those expected bytes as a decoding hint. They are checked
only after decoding in the regression test.

`samples/manifest.json` identifies the original session, window, checksum
and capture settings. This is one captured packet, not a delivery-rate trial.
The original 60-second session used separate 350 ms capture windows and had
about 58.9% elapsed RF coverage. Sample continuity inside a window does not
mean continuous reception across the whole session.

## Decode your recordings

```sh
python host/decode_recording.py YOUR_CAPTURE.iqs --frequency 2440.125 --sf 7 --output decoded.json
```

Input is our CRC32-protected `IQS1` format at 250 kcomplex samples/s,
BW203.125 kHz, sync0x12. Header/FEC/dewhitening and payload CRC use the pinned
[lora-phy 0.3.0](https://pypi.org/project/lora-phy/0.3.0/) dependency by Zhang
Maiyun, translated from jkadbear/LoRaPHY. Our adapter retains fractional
frequency correction until rounding and tries five bounded timing/FFT
hypotheses. Missing CRC, bad CRC, bad synchronization and truncated packets
are rejected. IQ sequence/sample gaps split the recording; they are never
padded or joined as if continuous.

SF7, SF8 and SF9 now have real RF recordings with image/capture hashes and
full CRC. A new independent 108-case live matrix decoded104 packets across
those SFs, four coding rates and lengths1/8/32. Per-SF counts are33/36,35/36,
36/36. All four misses and output-frame drops are retained in the report.
This decoder currently limits payloads to 250 bytes and requires sync0x12;
the transmitter's 255-byte and selectable-sync evidence does not apply here.

The [standalone ESP-IDF capture project](../firmware/iq-capture/README.md) is
included, with pinned dependencies and public source. It is a separate
application; portable Arduino IQ capture and native full-packet RX remain
unimplemented. **Do not flash the transmitter image expecting IQ capture.**

## Receive new live packets

Flash the capture image on the XIAO and the opt-in `aerolink-hf-tx` image on
the verified LR2021 board. Both ports must be free. For one new packet at
each of SF7/8/9, from the repository root:

```sh
python host/live_packets.py --xiao-port YOUR_XIAO_PORT --radio-port YOUR_LR2021_PORT \
  --repeats 1 --output my-live-packets
```

For a shuffled fresh-payload matrix matching the default-image experiment:

```sh
python host/live_packets.py --xiao-port YOUR_XIAO_PORT --radio-port YOUR_LR2021_PORT \
  --sfs 7,8,9 --crs 1,2,3,4 --lengths 1,8,32 --repeats 3 --seed 2026100732 \
  --window-ms 500 --output my-live-matrix
```

Every trial sends once and preserves its actual IQ window, SHA256, TX IRQ
diagnostic, expected bytes, blind decoded bytes and failures. Use optional
`--xiao-image`, `--radio-image`, `--xiao-source`, `--radio-source` to record
the exact tested build. Output must be a new directory; existing data is never
overwritten. A serial timeout retains partial lines until newline.

This workflow intentionally captures separate windows. The real RF coverage
inside each window is reported, including USB drops; it does not promise
continuous full-time reception or100% retained samples. A valid chip TX_DONE
still needs independent XIAO IQ/PC CRC proof before a trial passes.

中文：示例 IQ 来自真实 LR2021 发射、XIAO 接收；完整包和 CRC 在电脑上解码。
下载后可直接复现该录音。它不是 ESP32 内部完整解码，也不代表连续全时接收。
