"""ESP32 application receives and replies by itself; host only drives LR2021."""
import argparse,hashlib,json,pathlib,random,time
from datetime import datetime,timezone
from verify_native_levels import openport,line,command

def main():
    p=argparse.ArgumentParser();p.add_argument('--xiao',default='COM3');p.add_argument('--lr2021',default='COM4')
    p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--seed',type=int,default=202610071051)
    p.add_argument('--count',type=int,default=8)
    p.add_argument('--xiao-image',type=pathlib.Path)
    p.add_argument('--lr-image',type=pathlib.Path)
    a=p.parse_args()
    if a.output.exists():p.error('output must be new')
    if not 1<=a.count<=8:p.error('count must be 1..8')
    report=dict(startedUtc=datetime.now(timezone.utc).isoformat(),seed=a.seed,retries=0,
                xiaoSerialWrites=0,pcDecoder=False,example='NativeDuplex/xiao-echo',cases=[])
    for name,path in (('xiaoImage',a.xiao_image),('lrImage',a.lr_image)):
        if path:
            raw=path.read_bytes();report[name]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
    rng=random.Random(a.seed);x=openport(a.xiao,115200);r=openport(a.lr2021,115200)
    try:
        for c in ('FREQ 2440125','BW 203125','PRE 16','SYNC 18','INV 0'):
            reply,_=command(r,c,c.split()[0]);assert reply==c+' status=0',reply
        settings=[(cr,n) for cr in (1,2,3,4) for n in (8,32)];rng.shuffle(settings)
        for cr,n in settings[:a.count]:
            data=rng.randbytes(n);reply=b'ACK:'+data
            case=dict(cr=cr,length=n,expectedHex=data.hex(),expectedReplyHex=reply.hex(),xiao=[],lr2021=[],rxPassed=False,txPassed=False,strictRfTxPassed=False)
            report['cases'].append(case) # retain the current attempt on timeout
            # The autonomous receiver prints a marker every window. Discard
            # markers queued while LR2021 was configured or a prior ACK read.
            x.reset_input_buffer();x._line_buffer=bytearray()
            deadline=time.monotonic()+30
            while time.monotonic()<deadline:
                s=line(x,1)
                if s:case['xiao'].append(s)
                if s.startswith('LISTEN '):break
            else:raise TimeoutError(case)
            time.sleep(.06)
            tx,lines=command(r,f'TX 7 {cr} {data.hex()}','TX_PUBLIC');case['lr2021'].extend(lines)
            deadline=time.monotonic()+10
            while time.monotonic()<deadline:
                s=line(x,.2)
                if s:case['xiao'].append(s)
                if s.startswith('NATIVE_ACK'):break
            deadline=time.monotonic()+2
            while time.monotonic()<deadline:
                s=line(r,.2)
                if s:case['lr2021'].append(s)
                if s.startswith('RX_IRQ'):break
            case['rxPassed']=tx.startswith('TX_PUBLIC status=0 ') and any(s==f'NATIVE_RX crc_ok=1 bytes={n} hex={data.hex()}' for s in case['xiao'])
            case['txPassed']=any(s==f'NATIVE_ACK status=ok bytes={n+4}' for s in case['xiao']) and any('crc_ok=1' in s and s.endswith('hex='+reply.hex()) for s in case['lr2021'])
            # Independent RF gate, separate from the existing console+RF score.
            # Match the readout with its following IRQ; reject mixed error flags.
            case['strictRfTxPassed']=False
            pending=None
            for s in case['lr2021']:
                if s.startswith('RX '):pending=s
                elif s.startswith('RX_IRQ ') and pending:
                    irq=int(s.split()[1],16)
                    required=(1<<4)|(1<<5)|(1<<18)
                    errors=(1<<9)|(1<<22)
                    if (irq&required)==required and not irq&errors and pending.startswith('RX status=0 header=0 crc_present=1 crc_ok=1 ') and pending.endswith('hex='+reply.hex()) and f'bytes={n+4} ' in pending:
                        case['strictRfTxPassed']=True
                    pending=None
            print(len(report['cases']),cr,n,'RX',case['rxPassed'],'ACK',case['txPassed'],'strict RF',case['strictRfTxPassed'],flush=True)
    except BaseException as error:
        report['error']=repr(error);raise
    finally:
        x.close();r.close();report['finishedUtc']=datetime.now(timezone.utc).isoformat()
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
if __name__=='__main__':main()
