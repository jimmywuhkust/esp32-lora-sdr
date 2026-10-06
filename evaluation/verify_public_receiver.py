"""Independent RadioLib RX proof: no AeroLink private source/protocol required.

Run only with serial monitors and web controllers disconnected. No RF retries.
Every case records native local completion, fresh RX line, CRC presence/status,
full received hex and exact-byte comparison. Default sends 24 bounded packets.
"""
import argparse, hashlib, json, random, re, time
from datetime import datetime, timezone
from pathlib import Path
import serial

ROOT=Path(__file__).resolve().parent.parent
RX=re.compile(r'RX status=(-?\d+) header=(-?\d+) crc_present=([01]) crc_ok=([01]) bytes=(\d+) rssi=([\d.-]+) snr=([\d.-]+) hex=([0-9a-f]*)$')

def open_port(name):
    port=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=5)
    port.dtr=port.rts=False;port.port=name;port.open();return port

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--tx-port',required=True);p.add_argument('--rx-port',required=True)
    p.add_argument('--repeats',type=int,default=1);p.add_argument('--seed',type=int,default=20261007)
    p.add_argument('--sender',choices=['bench','sendonce'],default='bench')
    p.add_argument('--smoke',action='store_true',help='one fresh 32-byte CR4/8 packet per block')
    p.add_argument('--sf',type=int,choices=[7,8,9],default=7)
    p.add_argument('--bandwidth',type=int,choices=[203125,406250,812500],default=203125)
    p.add_argument('--window',type=int,default=15000)
    p.add_argument('--transport',choices=['DAC','STREAM'],default='DAC')
    p.add_argument('--gap',type=int,default=14)
    p.add_argument('--tx-image',type=Path,help='override image hash for a separately built research transmitter')
    p.add_argument('--tx-source',type=Path,help='source directory of the separately built research transmitter')
    p.add_argument('--output',type=Path,default=ROOT/'evaluation/data/public-receiver.json')
    a=p.parse_args()
    if not 1<=a.repeats<=100:raise ValueError('repeats must be 1..100')
    rng=random.Random(a.seed)
    configs=[(cr,n) for cr in range(1,5) for n in [1,8,32,80,128,255]]
    if a.smoke:configs=[(4,32)]
    result=dict(startedUtc=datetime.now(timezone.utc).isoformat(),seed=a.seed,
        txPort=a.tx_port,rxPort=a.rx_port,sender=a.sender,completed=False,cases=[],transport=a.transport,gapSamples=a.gap,
        profile=dict(frequencyHz=2440125000,bandwidthHz=a.bandwidth,sf=a.sf,sync=18,preamble=16,windowSamples=a.window),
        rule='fresh RX status=0, header=0, CRC present=1 and CRC ok=1; exact full payload bytes',
        sha256={str(f.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(f.read_bytes()).hexdigest()
            for f in [ROOT/f'.pio/build/{"xiao-s3" if a.sender=="bench" else "xiao-send-once"}/firmware.bin',
                ROOT/'companion/lr2021/.pio/build/aerolink-hf/firmware.bin'] if f.exists()})
    if a.tx_image:
        result['sha256'].pop(f'.pio/build/{"xiao-s3" if a.sender=="bench" else "xiao-send-once"}/firmware.bin',None)
        result['sha256'][str(a.tx_image).replace('\\','/')]=hashlib.sha256(a.tx_image.read_bytes()).hexdigest()
    result['firmwareHashMethod']='SHA256 of last-flashed input images; esptool write verification is separate'
    source=a.tx_source or (ROOT/'research/full-symbol-dac-fast/src' if a.transport=='STREAM' else ROOT/'src')
    result['sourceSha256']={f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(source.glob('*')) if f.is_file()}
    tx=rx=None
    def line(port,timeout):
        end=time.monotonic()+timeout
        buffered=bytearray()
        while time.monotonic()<end:
            data=port.readline()
            buffered.extend(data)
            if buffered.endswith(b'\n'):return buffered.decode('utf-8',errors='replace').strip()
        if buffered:return buffered.decode('utf-8',errors='replace').strip()
        return ''
    try:
        tx=open_port(a.tx_port);rx=open_port(a.rx_port)
        tx.reset_input_buffer();rx.reset_input_buffer()
        rx.write(b'INFO\n');result['receiverIdentity']=line(rx,5)
        if not result['receiverIdentity'].startswith('LR2021_PUBLIC status=0 ready=1'):
            raise RuntimeError('Independent receiver not ready: '+result['receiverIdentity'])
        if a.sender=='bench':
            commands=[('INFO','LoRaSDR native S3 0.1'),(a.transport,a.transport+' selected'),('AMP 150','AMP 150')]
            commands.extend((cmd,cmd) for cmd in ['FREQ 2440125','CFO 15000','PRE 16',f'WIN {a.window}'])
            if a.transport!='STREAM':commands.append((f'BW {a.bandwidth}',f'BW {a.bandwidth}'))
            if a.transport=='STREAM':commands.append((f'GAP {a.gap}',f'GAP {a.gap}'))
            for command,expected in commands:
                tx.write((command+'\n').encode());reply=line(tx,5)
                if reply!=expected:raise RuntimeError(f'{command}: {reply}')
        # Restore SF7 too: a prior research run may have left RX at SF8/SF9.
        rx.write(f'SF {a.sf}\n'.encode());reply=line(rx,5)
        if reply!=f'SF {a.sf} status=0':raise RuntimeError(reply)
        rx.write(f'BW {a.bandwidth}\n'.encode());reply=line(rx,5)
        if reply!=f'BW {a.bandwidth} status=0':raise RuntimeError(reply)
        for block in range(a.repeats):
            order=configs.copy();rng.shuffle(order)
            if a.sender=='sendonce':order=[(4,16)]
            for cr,n in order:
                data=b'Hello from XIAO!' if a.sender=='sendonce' else rng.randbytes(n)
                rx.reset_input_buffer()
                case=dict(block=block,cr=cr,length=n,expected=data.hex(),received=[],
                    startedUtc=datetime.now(timezone.utc).isoformat())
                tx.write(b's' if a.sender=='sendonce' else f'TX {a.sf} {cr} {data.hex()}\n'.encode())
                case['start']=line(tx,10)
                case['end']=line(tx,10)
                case['transportOk']=case['start'].startswith('TX ok;') if a.sender=='sendonce' else (
                    case['start']=='TXSTART NATIVE' and case['end'].startswith('TXEND NATIVE ok '))
                # Drain all lines after the synchronous TX. The receiver runs
                # independently while TX blocks; no expected bytes are sent to it.
                end=time.monotonic()+.5
                while time.monotonic()<end:
                    raw=line(rx,.15)
                    if raw:
                        match=RX.fullmatch(raw)
                        item=dict(raw=raw)
                        if match:
                            status,header,present,ok,length,rssi,snr,hexdata=match.groups()
                            item.update(status=int(status),headerStatus=int(header),crcPresent=present=='1',
                                crcOk=ok=='1',bytes=int(length),rssi=float(rssi),snr=float(snr),hex=hexdata)
                        case['received'].append(item)
                case['passed']=case['transportOk'] and any(q.get('status')==0 and q.get('headerStatus')==0 and
                    q.get('crcPresent') and q.get('crcOk') and q.get('bytes')==n and q.get('hex')==data.hex()
                    for q in case['received'])
                rx.write(b'INFO\n');case['receiverAfter']=line(rx,3)
                result['cases'].append(case)
                checkpoint=a.output.with_suffix('')
                checkpoint.mkdir(parents=True,exist_ok=True)
                (checkpoint/f'case-{len(result["cases"]):05d}.json').write_text(json.dumps(case,indent=2),encoding='utf-8')
                print(len(result['cases']),cr,n,case['passed'],flush=True)
                # Continuing after a missing TXEND would shift every later
                # request/reply and attribute the previous packet to a new job.
                # Preserve this failed trial and stop instead of cascading.
                if not case['transportOk']:raise RuntimeError('Native serial transaction lost synchronization')
                if 'ready=1' not in case['receiverAfter']:raise RuntimeError('Independent receiver stopped')
        result['completed']=True
    except BaseException as error:
        result['error']=repr(error);raise
    finally:
        if tx:tx.close()
        if rx:rx.close()
        result['finishedUtc']=datetime.now(timezone.utc).isoformat()
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(a.output.resolve(),flush=True)

if __name__=='__main__':main()
