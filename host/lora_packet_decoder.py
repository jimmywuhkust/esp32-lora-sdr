"""CRC-gated PC LoRa decoding of contiguous XIAO I/Q, using lora-phy 0.3.0.

The pinned library provides header/FEC/deinterleaving/dewhitening/CRC.
This adapter preserves fractional symbol correction until rounding, refuses
truncated packets and tries five bounded FFT/timing hypotheses. No expected
payload is supplied to the receiver. Only sync 0x12 + valid header + CRC-on
packets with a matching payload CRC are returned.
"""
import logging
import numpy as np
from scipy.signal import resample_poly
from lora_phy import LoRaReceiver
from lora_phy.common import calc_sym_num
from lora_phy.errors import LoRaPHYError, NoPreambleError


class PacketReceiver(LoRaReceiver):
    def __init__(self, frequency, sf, fold='magnitude', timing=0):
        super().__init__(frequency, sf, 203125, 250000)
        self.fold = fold
        self.timing = timing
        self.correction = 0

    def _dechirp(self, signal, x, is_up=True):
        if x < 0 or x+self._sample_num > len(signal):
            raise IndexError('Incomplete chirp window')
        chp = self._down_chirp if is_up else self._up_chirp
        part = signal[x:x+self._sample_num]
        if self.correction:
            part = part*np.exp(-2j*np.pi*self.correction*np.arange(len(part))/(2*self._bandwidth))
        ft = np.fft.fft(part*chp, self._fft_len)
        a, b = ft[:self._bin_num], ft[-self._bin_num:]
        powers = np.abs(a+b) if self.fold == 'coherent' else np.abs(a)+np.abs(b)
        best = int(np.argmax(powers))
        return float(powers[best]), best

    def packets(self, signal):
        """Yield complete symbol packets without losing earlier packets at EOF."""
        n = self._sample_num
        cursor = 0
        while cursor+8*n <= len(signal):
            self.correction = 0
            try:
                detected = self._detect(signal, cursor)
            except NoPreambleError:
                break
            try:
                start, ref, cfo = self._sync(signal, detected)
                start += self.timing
                if self.fold == 'coherent':
                    self.correction = cfo
                    _, ref = self._dechirp(signal, round(start-6.25*n))
                else:
                    ref = (ref+self.timing*5)%self._bin_num
                if start < cursor or start+8*n > len(signal):
                    cursor = max(cursor+n, detected+n)
                    continue
                symbols = np.array([self._peak_idx_to_symbol(self._dechirp(signal,start+i*n)[1],ref)%(1<<self._spreading_factor) for i in range(8)],dtype=np.uint16)
                valid, length, cr, crc = self._parse_header(symbols)
                if not valid or not 1 <= length <= 250 or not 1 <= cr <= 4:
                    cursor = max(cursor+n, start+7*n)
                    continue
                count = calc_sym_num(True,length,cr,crc,self._spreading_factor,self._proc.low_data_rate_optimization)
                if start+count*n > len(signal):
                    cursor = max(cursor+n,start+7*n)
                    continue
                rest = [self._peak_idx_to_symbol(self._dechirp(signal,start+i*n)[1],ref)%(1<<self._spreading_factor) for i in range(8,count)]
                symbols = np.concatenate([symbols,np.array(rest,dtype=np.uint16)])
                # Direct uint16 casting truncates small positive CFO correction
                # into a whole-bin subtraction. Round floating values first.
                symbols = np.rint(self._proc.dynamic_compensation(symbols,cfo)).astype(np.uint16)%(1<<self._spreading_factor)
                netids = [self._peak_idx_to_symbol(self._dechirp(signal,round(start-offset*n))[1],ref)%(1<<self._spreading_factor) for offset in (4.25,3.25)]
                yield start,symbols,float(cfo),netids
                cursor = start+count*n
            except (LoRaPHYError,IndexError,ValueError):
                cursor = max(cursor+n,detected+n)


class Decoder:
    def __init__(self, frequency=2440.125, sf=7, invert=False):
        if not 7 <= sf <= 9:
            raise ValueError('PC packet decoding currently supports SF7–SF9')
        self.frequency = frequency
        self.sf = sf
        self.invert = invert
        self.receivers = [PacketReceiver(round(frequency*1e6),sf,fold,timing) for fold,timing in (('magnitude',0),('magnitude',-2),('magnitude',1),('coherent',0),('coherent',-2))]
        self.stats = dict(segments=0,headerCandidates=0,crcValid=0,crcRejectedCandidates=0,crcAbsent=0,syncRejected=0)

    def decode(self, iq, sample_index=0):
        self.stats['segments'] += 1
        if len(iq) < self.receivers[0]._sample_num*8*250000/406250:
            return []
        iq = np.asarray(iq,dtype=np.complex64)
        if self.invert:
            iq = np.conj(iq)
        signal = resample_poly(self.receivers[0].lowpass(iq),13,8)
        packets = []
        for rx in self.receivers:
            for position,sym,cfo,netids in rx.packets(signal):
                try:
                    valid,length,cr,has_crc = rx._parse_header(sym[:8])
                    if not valid:
                        continue
                    self.stats['headerCandidates'] += 1
                    if any(abs((v-target+(1<<self.sf)//2)%(1<<self.sf)-(1<<self.sf)//2)>1 for v,target in zip(netids,(8,16))):
                        self.stats['syncRejected'] += 1
                        continue
                    if not has_crc:
                        self.stats['crcAbsent'] += 1
                        continue
                    data,expected = rx.decode(sym)
                    if expected is None or len(data) != length+2 or tuple(data[-2:]) != expected:
                        self.stats['crcRejectedCandidates'] += 1
                        continue
                    payload = data[:-2].tobytes()
                    index = sample_index+round(position*8/13)
                    # Deduplicate timing hypotheses; retain distinct RF packets
                    # even when they carry identical payloads.
                    tolerance = 2**self.sf*250000/203125
                    if any(p['hex']==payload.hex() and abs(p['sampleIndex']-index)<tolerance for p in packets):
                        continue
                    self.stats['crcValid'] += 1
                    packets.append(dict(text=payload.decode('utf-8','replace'),hex=payload.hex(),bytes=len(payload),crcOk=True,crcHex=data[-2:].tobytes().hex(),sf=self.sf,codingRate=f'4/{cr+4}',frequency=self.frequency,cfoHz=round(cfo,2),syncBins=netids,sampleIndex=index,segmentSampleIndex=sample_index,segmentSamples=len(iq),hypothesis=f'{rx.fold}/{rx.timing}',source='XIAO I/Q → PC LoRa decoder'))
                except (LoRaPHYError,ValueError,IndexError):
                    self.stats['crcRejectedCandidates'] += 1
        return sorted(packets,key=lambda p:p['sampleIndex'])


logging.getLogger('lora_phy').setLevel(logging.ERROR)
