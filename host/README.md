# PC packet decoder and real RF recording

This companion decodes complete LoRa bytes and checks payload CRC from
**XIAO-captured I/Q on a PC**. It is not native Arduino packet reception.
The transmitter library and this offline receiver workflow are separate.

## Try it without hardware

Use Python 3.13 with the pinned dependencies. From the repository root:

```sh
python -m pip install -r host/requirements.txt
python host/decode_recording.py host/samples/lr2021-sf7-window.iqs
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

SF7 is demonstrated by this real recording. SF8/SF9 decoder checks use
synthetic signals and do not establish real XIAO reception at those SFs.
This decoder currently limits payloads to 250 bytes and requires sync0x12;
the transmitter's 255-byte and selectable-sync evidence does not apply here.

The repository does not yet provide a portable Arduino I/Q capture backend
or native full-packet RX. Its sample came from our separate modified ESP-SDR
capture firmware. **Do not flash the transmitter image expecting IQ capture.**
This companion makes the existing receive evidence reproducible without
claiming an unimplemented integrated transceiver API.

中文：示例 IQ 来自真实 LR2021 发射、XIAO 接收；完整包和 CRC 在电脑上解码。
下载后可直接复现该录音。它不是 ESP32 内部完整解码，也不代表连续全时接收。
