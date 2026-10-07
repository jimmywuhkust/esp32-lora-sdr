"""Record source identity and all five compiled Arduino component images.

The staging path accommodates Windows CMake's non-ASCII path limitations.
Refuse to label an image set if its staged source differs from this checkout.
This records build identity; it does not claim an RF test or reproducible binary.
"""
import argparse,hashlib,json,pathlib
from datetime import datetime,timezone
ROOT=pathlib.Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    p=argparse.ArgumentParser()
    p.add_argument('--staging',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('retain earlier build identities; use a new output')
    # Same audited source set as the preceding measured build; include every
    # local header/source/configuration used by this component profile.
    previous=json.loads((ROOT/'evaluation/data/arduino-build-soft-irq.json').read_text())
    sources={}
    for name in previous['sources']:
        content=(ROOT/name).read_bytes().replace(b'\r\n',b'\n')
        staged=(a.staging/name).read_bytes().replace(b'\r\n',b'\n')
        if content!=staged:raise RuntimeError('staged source mismatch: '+name)
        sources[name]=sha(content)
    images={}
    for profile in ('rx','echo','bench','ping','pong'):
        directory=a.staging/'examples/ArduinoDuplex/.pio/build'/('xiao-arduino-'+profile)
        images[profile]={}
        for filename in ('bootloader.bin','partitions.bin','firmware.bin'):
            content=(directory/filename).read_bytes()
            images[profile][filename]=dict(bytes=len(content),sha256=sha(content))
    report=dict(recordedUtc=datetime.now(timezone.utc).isoformat(),
        normalization='CRLF to LF',sources=sources,
        sourceFingerprint=sha(json.dumps(sources,sort_keys=True,separators=(',',':')).encode()),
        framework=dict(arduino='2.0.17',espIdf='4.4.7',platform='espressif32@7.0.1'),
        profiles=images,scope='compiled images and matched staged source; RF results are separate datasets')
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')
    print(report['sourceFingerprint'],len(sources),'matched source inputs')
if __name__=='__main__':main()
