"""Real RF golden capture plus negative CRC, truncation and gap regressions."""
import json, struct, tempfile, unittest, zlib
from pathlib import Path
from unittest.mock import patch
import numpy as np
from scipy.signal import resample_poly
from lora_phy import LoRaTransmitter
from decode_recording import decode
from lora_packet_decoder import Decoder
from lora_packet_iq import HEADER, decode_frame, contiguous_runs

HERE=Path(__file__).resolve().parent

def synthetic(payload,sf=7,cfo=0,crc=True,bad_crc=False):
    tx=LoRaTransmitter(sf,203125,406250,coding_rate=1,enable_crc=crc,preamble_len=16)
    data=np.frombuffer(payload,dtype=np.uint8).copy()
    if bad_crc:
        with patch('lora_phy.tx.calc_crc',return_value=(np.uint8(0),np.uint8(0))):symbols=tx.encode(data)
    else:symbols=tx.encode(data)
    n=1<<sf;t=np.arange(2*n)/2;up=np.exp(1j*np.pi*(t*t/n-t))
    parts=[up]*16+[np.roll(up,-16),np.roll(up,-32),up.conj(),up.conj(),up.conj()[:n//2]]
    parts.extend(np.roll(up,-2*int(symbol)) for symbol in symbols)
    iq=np.concatenate(parts);iq*=np.exp(2j*np.pi*cfo*np.arange(len(iq))/406250)
    return np.concatenate([np.zeros(1200),resample_poly(iq,8,13),np.zeros(2000)])

class DecoderTests(unittest.TestCase):
    def test_real_rf_recording_without_expected_payload_hint(self):
        result=decode(HERE/'samples/lr2021-sf7-window.iqs')
        expected=json.loads((HERE/'samples/manifest.json').read_text(encoding='utf-8'))
        self.assertEqual(result['iqSha256'],expected['sha256'])
        self.assertEqual([p['hex'] for p in result['packets']],[expected['expectedPayloadHex']])
        self.assertEqual(result['packets'][0]['crcHex'],expected['expectedPayloadCRC'])
        self.assertTrue(result['packets'][0]['crcOk'])
        self.assertEqual(result['contiguousRuns'],1)

    def test_synthetic_higher_sf_and_frequency_offsets(self):
        payload=b'Synthetic decoder regression'
        for sf in (7,8,9):
            for cfo in (-13000,13000):
                with self.subTest(sf=sf,cfo=cfo):
                    result=Decoder(sf=sf).decode(synthetic(payload,sf,cfo))
                    self.assertEqual([p['hex'] for p in result],[payload.hex()])

    def test_missing_or_bad_payload_crc_never_returns_packet(self):
        for options in (dict(crc=False),dict(bad_crc=True)):
            with self.subTest(options=options):
                self.assertEqual(Decoder().decode(synthetic(b'CRC required',**options)),[])

    def test_noise_and_truncation_never_return_packet(self):
        rng=np.random.default_rng(7)
        self.assertEqual(Decoder().decode(rng.normal(size=10000)+1j*rng.normal(size=10000)),[])
        iq=synthetic(b'An incomplete payload must never be silently padded')
        self.assertEqual(Decoder().decode(iq[:len(iq)//2]),[])

    def test_transport_corruption_is_rejected_before_decoding(self):
        raw=bytearray((HERE/'samples/lr2021-sf7-window.iqs').read_bytes());raw[30]^=1
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'corrupt.iqs';path.write_bytes(raw)
            with self.assertRaisesRegex(ValueError,'CRC'):decode(path)

    def test_gaps_split_runs_and_backwards_indices_are_rejected(self):
        def frame(sequence,index,flags=0):
            raw=HEADER.pack(b'IQS1',sequence,index,2,4,flags,64,40,9)+bytes([0x87,0xf1])
            return decode_frame(raw+struct.pack('<I',zlib.crc32(raw)))
        first=frame(0,0)
        for second in (frame(2,2),frame(1,3),frame(1,2,1),frame(1,2,2)):
            self.assertEqual(len(list(contiguous_runs([first,second]))),2)
        self.assertEqual(len(list(contiguous_runs([first,frame(1,2)]))),1)
        with self.assertRaisesRegex(ValueError,'backwards'):list(contiguous_runs([first,frame(1,1)]))

if __name__=='__main__':unittest.main()
