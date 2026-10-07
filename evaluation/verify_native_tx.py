"""Native MCU TX, fresh LR2021 RX epochs, strict CRC and exact bytes.

The host supplies payload bytes only. No host encoder or IQ upload is used.
Retain every attempted packet, including failures and pre-rearm IRQ state.
"""
import argparse
import hashlib
import json
import pathlib
import random
import re
import time
from datetime import datetime, timezone
from verify_native_levels import openport, line, command

ROOT = pathlib.Path(__file__).resolve().parents[1]
RX = re.compile(r'RX status=0 header=0 crc_present=1 crc_ok=1 bytes=(\d+) rssi=([\d.-]+) snr=([\d.-]+) hex=([0-9a-f]+)$')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--xiao', default='COM3')
    p.add_argument('--lr2021', default='COM4')
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--seed', type=int, default=202610071155)
    p.add_argument('--repeats', type=int, default=1)
    p.add_argument('--image', type=pathlib.Path,
                   default=ROOT/'firmware/iq-capture/prebuilt/native/application.bin')
    p.add_argument('--lr-image', type=pathlib.Path,
                   default=ROOT/'companion/lr2021/.pio/build/aerolink-hf-tx/firmware.bin')
    a = p.parse_args()
    if a.output.exists(): p.error('output must be new; retain failed attempts')
    if not 1 <= a.repeats <= 20: p.error('repeats must be 1..20')
    rng = random.Random(a.seed)
    report = dict(startedUtc=datetime.now(timezone.utc).isoformat(), seed=a.seed,
                  retries=0, pcDecoder=False, hostIqUploaded=False, completed=False,
                  profile=dict(frequencyMHz=2440.125, bandwidthHz=203125, sf=7,
                               preamble=16, sync=18, relativePowerPercent=75), cases=[])
    for name, image in [('xiaoImage', a.image), ('lrImage', a.lr_image)]:
        if image.exists():
            raw = image.read_bytes()
            report[name] = dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
    def save():
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    x = openport(a.xiao, 2000000)
    r = openport(a.lr2021, 115200)
    case = None
    try:
        x.write(b'\nINFO\n')
        until = time.monotonic()+2
        while time.monotonic() < until: line(x, .2)
        report['xiaoIdentity'], _ = command(x, 'INFO', 'S3SDR ')
        report['lrIdentity'], _ = command(r, 'INFO', 'LR2021_PUBLIC ')
        for c in ('FREQ 2440125', 'BW 203125', 'PRE 16', 'SYNC 18', 'INV 0', 'CRC 1', 'SF 7'):
            reply, _ = command(r, c, c.split()[0])
            assert reply == c+' status=0', reply
        for c in ('FREQ 2440125', 'BW 203125', 'PRE 16', 'SYNC 18', 'CFO 15000', 'POWER 75'):
            reply, _ = command(x, 'LSET '+c, 'LSET ')
            assert reply == 'LSET '+c+' status=ok', reply
        settings = [(cr, n) for cr in (1, 2, 3, 4) for n in (1, 8, 32, 80, 255)]*a.repeats
        rng.shuffle(settings)
        for cr, n in settings:
            data = rng.randbytes(n)
            case = dict(sf=7, codingRate=cr, bytes=n, expectedHex=data.hex())
            _, case['beforeRearm'] = command(r, 'INFO', 'LR2021_PUBLIC ')
            # A fresh explicit RX interval, not a weakened CRC rule. The
            # original receiver firmware still rejects any mixed error IRQ.
            reply, case['rearm'] = command(r, 'SF 7', 'SF 7 ')
            assert reply == 'SF 7 status=0', reply
            _, case['xiao'] = command(x, f'TX 7 {cr} {data.hex()}', 'TXEND ')
            received = []
            until = time.monotonic()+1.5
            while time.monotonic() < until:
                s = line(r, .15)
                if s: received.append(s)
                if s.startswith('RX_IRQ') and any(
                    t.startswith('RX ') and t.endswith('hex='+data.hex()) for t in received): break
            case['lr2021'] = received
            exact = [s for s in received if s.startswith('RX ') and s.endswith('hex='+data.hex())]
            accepted = [m for s in exact if (m := RX.fullmatch(s)) and int(m[1]) == n]
            case['exactBytes'] = bool(exact)
            case['passed'] = any(s.startswith('TXEND NATIVE ok ') for s in case['xiao']) and bool(accepted)
            report['cases'].append(case)
            print(len(report['cases']), 'CR4/'+str(cr+4), n, case['passed'], flush=True)
            case = None
            save()
        report['completed'] = True
    except BaseException as exc:
        report['error'] = repr(exc)
        if case is not None: report['incompleteCase'] = case
        raise
    finally:
        x.close(); r.close()
        report['finishedUtc'] = datetime.now(timezone.utc).isoformat()
        save()


if __name__ == '__main__': main()
