"""Bounded fresh-payload RF matrix: channel, preamble, sync and IQ polarity.

Use SerialBench and the public LR2021 receiver with their setting commands.
No retransmission. Exactly matched payload and independent hardware CRC required.
"""
import argparse, hashlib, itertools, json, random, re, time
from datetime import datetime, timezone
from pathlib import Path
from verify_public_receiver import RX, open_port

ROOT=Path(__file__).resolve().parent.parent

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--tx-port',default='COM3');p.add_argument('--rx-port',default='COM4')
    p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--seed',type=int,default=2026100706)
    p.add_argument('--smoke',action='store_true')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if not 1<=a.repeats<=10:raise ValueError('repeats 1..10')
    rng=random.Random(a.seed)
    configs=list(itertools.product([2403125,2440125,2476125],[12,16,32,64],[18,52],[0,1]))
    if a.smoke:configs=[(2440125,16,18,1),(2440125,16,18,0)]
    result=dict(startedUtc=datetime.now(timezone.utc).isoformat(),completed=False,
        seed=a.seed,profile=dict(sf=7,bandwidthHz=203125,cr=4,length=32,cfoHz=15000,amp=150,analogGainCode=0),
        rule='fresh LR2021 status=0 header=0 CRC present+valid and exact full payload; no RF retry',
        imageSha256={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in
            [ROOT/'.pio/build/xiao-s3/firmware.bin',ROOT/'companion/lr2021/.pio/build/aerolink-hf/firmware.bin']},
        sourceSha256={str(f.relative_to(ROOT)):hashlib.sha256(f.read_bytes()).hexdigest() for f in
            [ROOT/'examples/SerialBench/main.cpp',ROOT/'companion/lr2021/src/main.cpp',ROOT/'src/ESP32S3Radio.cpp']},
        cases=[])
    tx=rx=None
    def line(port,timeout=5):
        until=time.monotonic()+timeout;data=bytearray()
        while time.monotonic()<until:
            data.extend(port.readline())
            if data.endswith(b'\n'):return data.decode(errors='replace').strip()
        return data.decode(errors='replace').strip()
    def command(port,value,expected):
        port.write((value+'\n').encode());reply=line(port)
        if reply!=expected:raise RuntimeError(value+': '+reply)
    try:
        tx=open_port(a.tx_port);rx=open_port(a.rx_port)
        tx.reset_input_buffer();rx.reset_input_buffer()
        command(tx,'INFO','LoRaSDR native S3 0.1')
        rx.write(b'INFO\n');result['receiverIdentity']=line(rx)
        if 'status=0 ready=1' not in result['receiverIdentity']:raise RuntimeError(result['receiverIdentity'])
        for value in ['DAC','AMP 150','PA 0','BW 203125','WIN 15000','CFO 15000']:
            command(tx,value,'DAC selected' if value=='DAC' else value)
        for value in ['SF 7','BW 203125']:command(rx,value,value+' status=0')
        for block in range(a.repeats):
            order=configs.copy();rng.shuffle(order)
            for khz,pre,sync,inverted in order:
                for value in [f'FREQ {khz}',f'PRE {pre}',f'SYNC {sync}',f'INV {inverted}']:command(tx,value,value)
                # S3's measured inverted=true produces the normal LR2021 convention.
                for value in [f'FREQ {khz}',f'PRE {pre}',f'SYNC {sync}',f'INV {1-inverted}']:command(rx,value,value+' status=0')
                data=rng.randbytes(32);rx.reset_input_buffer()
                case=dict(block=block,frequencyHz=khz*1000,preamble=pre,syncWord=sync,
                    xiaoInverted=inverted,lr2021InvertIQ=1-inverted,expected=data.hex(),received=[])
                tx.write(f'TX 7 4 {data.hex()}\n'.encode());case['start']=line(tx,10);case['end']=line(tx,10)
                case['transportOk']=case['start']=='TXSTART NATIVE' and case['end'].startswith('TXEND NATIVE ok ')
                until=time.monotonic()+.5
                while time.monotonic()<until:
                    raw=line(rx,.15)
                    if not raw:continue
                    q=dict(raw=raw);match=RX.fullmatch(raw)
                    if match:
                        status,header,present,ok,n,rssi,snr,hexdata=match.groups()
                        q.update(status=int(status),header=int(header),crcPresent=present=='1',crcOk=ok=='1',
                            bytes=int(n),rssi=float(rssi),snr=float(snr),hex=hexdata)
                    case['received'].append(q)
                case['passed']=case['transportOk'] and any(q.get('status')==0 and q.get('header')==0 and
                    q.get('crcPresent') and q.get('crcOk') and q.get('bytes')==32 and q.get('hex')==data.hex() for q in case['received'])
                result['cases'].append(case);print(len(result['cases']),khz,pre,sync,inverted,case['passed'],flush=True)
                # Unique per-case checkpoints avoid repeatedly replacing a
                # cloud-synchronized file while OneDrive holds its handle.
                checkpoint=a.output.with_suffix('');checkpoint.mkdir(parents=True,exist_ok=True)
                (checkpoint/f'case-{len(result["cases"]):05d}.json').write_text(json.dumps(case,indent=2),encoding='utf-8')
                if not case['transportOk']:raise RuntimeError(case['end'])
        result['completed']=True
    except BaseException as error:
        result['error']=repr(error);raise
    finally:
        # All settings are local/idempotent; these commands do not transmit RF.
        if tx and tx.is_open:
            for value in ['FREQ 2440125','PRE 16','SYNC 18','INV 1']:
                try:command(tx,value,value)
                except Exception as error:result.setdefault('restoreErrors',[]).append(str(error))
        if rx and rx.is_open:
            for value in ['FREQ 2440125','PRE 16','SYNC 18','INV 0']:
                try:command(rx,value,value+' status=0')
                except Exception as error:result.setdefault('restoreErrors',[]).append(str(error))
        if tx:tx.close()
        if rx:rx.close()
        result['finishedUtc']=datetime.now(timezone.utc).isoformat()
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(result,indent=2),encoding='utf-8')

if __name__=='__main__':main()
