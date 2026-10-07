"""Native LoRaRadio amplitude sweep with independent LR2021 CRC evidence."""
import argparse,hashlib,json,pathlib,random,re,time
from datetime import datetime,timezone
import serial

def openport(name,baud):
    port=serial.Serial(port=None,baudrate=baud,timeout=.02,write_timeout=3)
    port.dtr=port.rts=False;port.port=name;port.open();port.reset_input_buffer();return port
def line(port,seconds=5):
    end=time.monotonic()+seconds;buf=getattr(port,'_line_buffer',bytearray())
    while time.monotonic()<end:
        c=port.read(1)
        if c==b'\n':
            port._line_buffer=bytearray();return buf.decode(errors='replace').strip()
        buf.extend(c)
    port._line_buffer=buf
    return ''
def command(port,text,prefix):
    raw=(text+'\n').encode()
    # Bound USB command bursts, including 510-character payload hex. This
    # pacing changes the control transport only, never the emitted RF packet.
    for offset in range(0,len(raw),96):
        port.write(raw[offset:offset+96])
        if offset+96<len(raw):time.sleep(.005)
    lines=[];end=time.monotonic()+8
    while time.monotonic()<end:
        s=line(port,1)
        if s:lines.append(s)
        if s.startswith(prefix):return s,lines
    raise TimeoutError((text,lines))
def main():
    p=argparse.ArgumentParser();p.add_argument('--xiao',default='COM3');p.add_argument('--lr2021',default='COM4')
    p.add_argument('--output',type=pathlib.Path,required=True);p.add_argument('--seed',type=int,default=202610071042)
    p.add_argument('--attempts',type=int,default=4);a=p.parse_args()
    if not 1<=a.attempts<=20:p.error('attempts must be 1..20')
    if a.output.exists():p.error('output must be new; keep previous attempts')
    report=dict(startedUtc=datetime.now(timezone.utc).isoformat(),seed=a.seed,retries=0,
                powerUnit='uncalibrated DAC amplitude percent; not dBm',pcDecoder=False,cases=[])
    rng=random.Random(a.seed);settings=[v for v in (1,2,5,10,25,50,75,100) for _ in range(a.attempts)];rng.shuffle(settings)
    x=openport(a.xiao,2000000);r=openport(a.lr2021,115200)
    try:
        # Complete boot output may have left an unterminated USB console line.
        # Resynchronize before settings, and record no RF attempt at this step.
        x.write(b'\nINFO\n');deadline=time.monotonic()+2
        while time.monotonic()<deadline:line(x,.2)
        x.reset_input_buffer();x._line_buffer=bytearray()
        for c in ('FREQ 2440125','BW 203125','PRE 16','SYNC 18','INV 0','SF 7'):
            reply,_=command(r,c,c.split()[0]);assert reply==c+' status=0',reply
        for c in ('FREQ 2440125','BW 203125','PRE 16','SYNC 18','CFO 15000'):
            reply,_=command(x,'LSET '+c,'LSET ');assert reply=='LSET '+c+' status=ok',reply
        for level in settings:
            data=rng.randbytes(32);case=dict(percent=level,dacAmplitude=level*2,expectedHex=data.hex())
            reply,_=command(x,f'LSET POWER {level}','LSET ');assert reply==f'LSET POWER {level} status=ok',reply
            _,case['xiao']=command(x,f'TX 7 1 {data.hex()}','TXEND ')
            received=[];deadline=time.monotonic()+1.2
            while time.monotonic()<deadline:
                s=line(r,.2)
                if s:received.append(s)
                if s.startswith('RX_IRQ'):break
            case['lr2021']=received
            case['passed']=any(s.startswith('TXEND NATIVE ok ') for s in case['xiao']) and any('crc_ok=1' in s and 'hex='+data.hex() in s for s in received)
            case['exactBytes']=any(s.startswith('RX ') and s.endswith('hex='+data.hex()) for s in received)
            packet=next((s for s in received if s.startswith('RX ')),None)
            if packet:
                case['rssi']=float(re.search(r'rssi=([\d.-]+)',packet)[1]);case['snr']=float(re.search(r'snr=([\d.-]+)',packet)[1])
            report['cases'].append(case);print(len(report['cases']),level,case['passed'],case.get('rssi'),flush=True)
    finally:
        x.close();r.close();report['finishedUtc']=datetime.now(timezone.utc).isoformat()
        a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
if __name__=='__main__':main()
