"""Offline complete LoRa packets from CRC-checked contiguous XIAO IQS1 data."""
import argparse, hashlib, json
from pathlib import Path
from lora_packet_iq import frames_from_file, contiguous_runs
from lora_packet_decoder import Decoder

def decode(path,frequency=2440.125,sf=7,invert=False):
    frames=list(frames_from_file(path))
    if not frames:raise ValueError('Empty IQ recording')
    configuration=None
    for frame in frames:
        if frame['decimation']!=64:raise ValueError('Decoder requires 250000 samples/s (16 MHz / 64)')
        # Hardware AGC gain may legitimately change during a packet. Packed
        # quantization and sample-rate settings must remain consistent.
        current=tuple(frame[k] for k in ('bits','decimation','shift'))
        if configuration is not None and current!=configuration:raise ValueError('IQ configuration changed within recording')
        configuration=current
    receiver=Decoder(frequency,sf,invert);packets=[];runs=0;samples=0
    for index,iq in contiguous_runs(frames):
        runs+=1;samples+=len(iq);packets.extend(receiver.decode(iq,index))
    return dict(recording=str(path),iqSha256=hashlib.sha256(Path(path).read_bytes()).hexdigest(),
        source='Recorded XIAO I/Q; packet decoding executes on the PC',frequencyMHz=frequency,sf=sf,
        bandwidthHz=203125,sampleRate=250000,frames=len(frames),contiguousRuns=runs,samples=samples,
        packets=packets,stats=receiver.stats)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('recording',type=Path)
    p.add_argument('--frequency',type=float,default=2440.125)
    p.add_argument('--sf',type=int,choices=[7,8,9],default=7)
    p.add_argument('--invert',action='store_true')
    p.add_argument('--output',type=Path)
    a=p.parse_args();result=decode(a.recording,a.frequency,a.sf,a.invert)
    text=json.dumps(result,ensure_ascii=False,indent=2)
    if a.output:a.output.write_text(text+'\n',encoding='utf-8')
    print(text)

if __name__=='__main__':main()
