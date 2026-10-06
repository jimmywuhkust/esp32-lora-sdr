"""Hardware command/airtime guards: these requests must not key RF."""
import argparse, hashlib, json, time
from pathlib import Path
from verify_public_receiver import open_port, ROOT

p=argparse.ArgumentParser()
p.add_argument('--port',required=True)
p.add_argument('--output',type=Path,default=ROOT/'evaluation/data/config-guards.json')
a=p.parse_args()
checks=[('AMP 0','ERR setting'),('AMP 201','ERR setting'),('WIN 999','ERR setting'),
 ('PRE 11','ERR setting'),('BW 125000','ERR BW'),('FREQ 2399000','ERR setting'),
 ('CFO 50001','ERR setting'),('TX 7 4 ff trailing','ERR arguments'),
 ('TX 7 4 f','ERR length'),('TX 7 4 gg','ERR hex'),('TX 13 4 ff','ERR arguments'),
 ('TX 7 5 ff','ERR arguments')]
result={'cases':[],'method':'Invalid inputs and overlong PLL packet; no requested RF transmission',
 'firmwareSha256':hashlib.sha256((ROOT/'.pio/build/xiao-s3/firmware.bin').read_bytes()).hexdigest()}
with open_port(a.port) as port:
    port.reset_input_buffer()
    def read():
        buf=bytearray();end=time.monotonic()+5
        while time.monotonic()<end:
            buf.extend(port.readline())
            if buf.endswith(b'\n'):return buf.decode().strip()
        return buf.decode(errors='replace').strip()
    def command(value):port.write((value+'\n').encode());return read()
    result['identity']=command('INFO')
    for value,expected in checks:
        reply=command(value)
        result['cases'].append(dict(command=value,expected=expected,reply=reply,passed=reply==expected))
    result['pllSelect']=command('PLL')
    payload=bytes(range(255)).hex()
    for name,value in [('PLL airtime cap','TX 9 4 '+payload)]:
        first=command(value);last=read()
        result['cases'].append(dict(command=value,name=name,start=first,end=last,
            passed=first=='TXSTART NATIVE' and last.startswith('TXEND NATIVE unsupported 0 0 ')))
    result['dacSelect']=command('DAC')
    result['bwSelect']=command('BW 406250')
    result['windowSelect']=command('WIN 15000')
    first=command('TX 7 4 01');last=read()
    result['cases'].append(dict(name='Window longer than symbol',start=first,end=last,
        passed=first=='TXSTART NATIVE' and last.startswith('TXEND NATIVE unsupported 0 0 ')))
    result['restore']=[command(c) for c in ['BW 203125','WIN 15000','INFO']]
    result['passed']=all(c['passed'] for c in result['cases'])
a.output.parent.mkdir(parents=True,exist_ok=True)
a.output.write_text(json.dumps(result,indent=2),encoding='utf-8')
print(f'{sum(c["passed"] for c in result["cases"])}/{len(result["cases"])} guards passed')
if not result['passed']:raise SystemExit(1)
