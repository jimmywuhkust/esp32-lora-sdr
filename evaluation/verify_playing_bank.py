"""Digital bank readback on isolated LUT firmware; no RF keying or TX command."""
import argparse, hashlib, json, re, time
from datetime import datetime, timezone
from pathlib import Path
import serial

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--port',default='COM3')
    p.add_argument('--image',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    result=dict(startedUtc=datetime.now(timezone.utc).isoformat(),port=a.port,
        imageSha256=hashlib.sha256(a.image.read_bytes()).hexdigest(),
        rfKeyed=False,completed=False,cases=[])
    port=serial.Serial(port=None,baudrate=115200,timeout=.1,write_timeout=5)
    port.dtr=port.rts=False;port.port=a.port
    def line(timeout=5):
        until=time.monotonic()+timeout;data=bytearray()
        while time.monotonic()<until:
            data.extend(port.readline())
            if data.endswith(b'\n'):return data.decode(errors='replace').strip()
        return data.decode(errors='replace').strip()
    try:
        port.open();port.reset_input_buffer();port.write(b'INFO\n')
        result['identity']=line()
        if result['identity']!='LoRaSDR native S3 0.1':raise RuntimeError(result['identity'])
        port.write(b'LUTTEST\n');result['writerSelfTest']=line()
        for block in range(3):
            for command in ['BANKIDLE','BANKPLAY']:
                port.write((command+'\n').encode());reply=line()
                match=re.fullmatch(r'BANK ok=(\d+) bad=(\d+) first=(\d+) c=(\d+)',reply)
                case=dict(block=block,command=command,line=reply)
                if not match:raise RuntimeError(reply)
                case.update(zip(['engineCompleted','wrongWords','firstWrong','maxCopyCycles'],map(int,match.groups())))
                result['cases'].append(case);print(case,flush=True);time.sleep(.05)
        result['completed']=True
    finally:
        port.close();a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(result,indent=2),encoding='utf-8')

if __name__=='__main__':main()
