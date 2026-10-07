"""Three-radio RF trials. ESP32s encode/decode; host only schedules and checks.

Requires ArduinoDuplex's bench image on both ESP32-S3s and the public LR2021
TX/RX companion. Records every trial, strict LR IRQs, and capture continuity.
"""
import argparse, hashlib, json, pathlib, random, time
from datetime import datetime, timezone
from verify_native_levels import openport, line, command

def collect(port, prefix, seconds):
    out=[]; end=time.monotonic()+seconds
    while time.monotonic()<end:
        s=line(port,.2)
        if s: out.append(s)
        if s.startswith(prefix): return out
    return out

def strict_lr(lines, payload):
    packet=None
    for s in lines:
        if s.startswith('RX '): packet=s
        elif s.startswith('RX_IRQ ') and packet:
            irq=int(s.split()[1],16)
            if ((irq & ((1<<4)|(1<<5)|(1<<18)))==((1<<4)|(1<<5)|(1<<18))
                and not irq & ((1<<9)|(1<<22))
                and packet.startswith('RX status=0 header=0 crc_present=1 crc_ok=1 ')
                and f'bytes={len(payload)} ' in packet and packet.endswith('hex='+payload.hex())):
                return True
            packet=None
    return False

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--a',default='COM3');p.add_argument('--b',default='COM5')
    p.add_argument('--lr',default='COM4');p.add_argument('--image',type=pathlib.Path,required=True)
    p.add_argument('--lr-image',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,required=True)
    p.add_argument('--seed',type=int,default=202610071500)
    a=p.parse_args()
    if a.output.exists():p.error('use a new output; retain failures')
    report=dict(startedUtc=datetime.now(timezone.utc).isoformat(),seed=a.seed,
                pcDecoder=False,hostIqUploaded=False,retries=0,cases=[],completed=False,
                scope='host-scheduled RF trials; autonomous ping/pong is a separate example')
    for key,path in [('esp32Image',a.image),('lrImage',a.lr_image)]:
        raw=path.read_bytes();report[key]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
    report['ports']=dict(A=a.a,B=a.b,LR2021=a.lr)
    rng=random.Random(a.seed);ports={}
    def save():
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    try:
        ports={name:openport(port,115200) for name,port in report['ports'].items()}
        report['identities']={}
        for name,port in ports.items():
            port.write(b'\nINFO\n');collect(port,'__never__',2)
            report['identities'][name],_=command(port,'INFO','LR2021_PUBLIC ' if name=='LR2021' else 'S3SDR ')
            commands=('FREQ 2440125','BW 203125','PRE 16','SYNC 18')
            for c in commands:
                text=c if name=='LR2021' else 'LSET '+c
                reply,_=command(port,text,c.split()[0] if name=='LR2021' else 'LSET ')
                assert reply==text+(' status=0' if name=='LR2021' else ' status=ok'),reply
            for c in (('INV 0','CRC 1','SF 7') if name=='LR2021' else ('LSET CFO 15000','LSET POWER 75')):
                reply,_=command(port,c,c.split()[0]);assert reply.endswith('status=0' if name=='LR2021' else 'status=ok'),reply
        # TX before RX on each board helps expose state-transition failures.
        pairs=[('A','LR2021'),('B','LR2021'),('LR2021','A'),('LR2021','B'),('A','B'),('B','A')]
        for source,target in pairs:
            for cr,n in ((1,8),(4,8),(1,32),(4,32)):
                payload=rng.randbytes(n)
                case=dict(source=source,target=target,sf=7,cr=cr,bytes=n,expectedHex=payload.hex(),passed=False)
                report['cases'].append(case);save()
                sender,receiver=ports[source],ports[target]
                if target=='LR2021':
                    _,case['rearm']=command(receiver,'SF 7','SF 7 ')
                else:
                    _,case['rearm']=command(receiver,'RXPACK 7 500','RXPACK READY')
                    time.sleep(.06)
                _,case['tx']=command(sender,f'TX 7 {cr} {payload.hex()}',
                                      'TX_PUBLIC ' if source=='LR2021' else 'TXEND ')
                case['rx']=collect(receiver,'RX_IRQ ' if target=='LR2021' else 'RXPACKEND ',30)
                tx_ok=any(s.startswith('TX_PUBLIC status=0 ' if source=='LR2021' else 'TXEND NATIVE ok ') for s in case['tx'])
                if target=='LR2021':
                    case['strictCrcExact']=strict_lr(case['rx'],payload)
                    case['passed']=tx_ok and case['strictCrcExact']
                else:
                    case['packets']=[json.loads(s[9:]) for s in case['rx'] if s.startswith('RXPACKET ')]
                    case['decode']=next((json.loads(s[9:]) for s in case['rx'] if s.startswith('RXDECODE ')),{})
                    case['passed']=tx_ok and any(v.get('crcOk') and v.get('hex')==payload.hex() for v in case['packets'])
                print(len(report['cases']),source,'->',target,cr,n,case['passed'],flush=True);save()
        report['completed']=True
    except BaseException as e:
        report['error']=repr(e);raise
    finally:
        for port in ports.values():port.close()
        report['finishedUtc']=datetime.now(timezone.utc).isoformat();save()

if __name__=='__main__':main()
