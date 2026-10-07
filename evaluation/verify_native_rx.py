"""Fresh RF matrix: only the ESP32 decodes; host compares returned bytes.

Needs the native serial bench and the public LR2021 TX/RX companion.
The negative trials emit RF only when their name explicitly says so.
"""
import argparse
import hashlib
import json
import pathlib
import random
import time
from datetime import datetime, timezone
from verify_native_levels import openport, line, command


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--xiao', default='COM3')
    p.add_argument('--lr2021', default='COM4')
    p.add_argument('--output', type=pathlib.Path, required=True)
    p.add_argument('--seed', type=int, default=202610071107)
    p.add_argument('--image', type=pathlib.Path,
                   default=pathlib.Path(__file__).resolve().parents[1] /
                   'firmware/iq-capture/prebuilt/native/application.bin')
    p.add_argument('--lr-image', type=pathlib.Path,
                   default=pathlib.Path(__file__).resolve().parents[1] /
                   'companion/lr2021/.pio/build/aerolink-hf-tx/firmware.bin')
    a = p.parse_args()
    if a.output.exists():
        p.error('output must be new; retain failed trials')
    rng = random.Random(a.seed)
    report = dict(startedUtc=datetime.now(timezone.utc).isoformat(), seed=a.seed,
                  retries=0, pcDecoder=False, cases=[], completed=False)
    if a.image.exists():
        raw = a.image.read_bytes()
        report['xiaoImage'] = dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
    if a.lr_image.exists():
        raw = a.lr_image.read_bytes()
        report['lrImage'] = dict(bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest())
    x = openport(a.xiao, 2000000)
    r = openport(a.lr2021, 115200)
    try:
        x.write(b'\nINFO\n')
        until = time.monotonic() + 2
        while time.monotonic() < until:
            line(x, .2)
        report['xiaoIdentity'], _ = command(x, 'INFO', 'S3SDR ')
        report['xiaoCapabilities'], _ = command(x, 'CAPS', 'CAPS ')
        report['lrIdentity'], _ = command(r, 'INFO', 'LR2021_PUBLIC ')
        for c in ('FREQ 2440125', 'BW 203125', 'PRE 16', 'SYNC 18', 'INV 0', 'CRC 1'):
            reply, _ = command(r, c, c.split()[0])
            assert reply == c + ' status=0', reply
        for c in ('FREQ 2440125', 'BW 203125', 'PRE 16', 'SYNC 18', 'CFO 15000'):
            reply, _ = command(x, 'LSET ' + c, 'LSET ')
            assert reply == 'LSET ' + c + ' status=ok', reply
        settings = [(7, cr, n, 500) for cr in (1, 2, 3, 4) for n in (1, 8, 32, 80, 255)]
        settings += [(sf, cr, n, 500) for sf in (8, 9) for cr in (1, 4) for n in (8, 32)]
        settings += [(sf, 1, 8, 900) for sf in (10, 11, 12)]
        rng.shuffle(settings)
        tests = [dict(kind='packet', sf=sf, cr=cr, length=n, windowMs=ms)
                 for sf, cr, n, ms in settings]
        tests += [dict(kind=kind, sf=7, cr=4, length=n, windowMs=ms)
                  for kind, n, ms in (
                      ('no-rf', 8, 100), ('no-payload-crc', 32, 500),
                      ('wrong-sync', 32, 500), ('truncated-window', 255, 100))]
        for case in tests:
            report['incompleteCase'] = case
            kind = case['kind']
            data = rng.randbytes(case['length'])
            case['expectedHex'] = data.hex()
            case['expectAccept'] = kind == 'packet'
            if kind == 'no-payload-crc':
                reply, _ = command(r, 'CRC 0', 'CRC ')
                assert reply == 'CRC 0 status=0', reply
            if kind == 'wrong-sync':
                reply, _ = command(r, 'SYNC 52', 'SYNC ')
                assert reply == 'SYNC 52 status=0', reply
            _, case['xiao'] = command(x, f"RXPACK {case['sf']} {case['windowMs']}", 'RXPACK READY')
            time.sleep(.06)
            if kind != 'no-rf':
                _, case['lr2021'] = command(r, f"TX {case['sf']} {case['cr']} {data.hex()}", 'TX_PUBLIC ')
            else:
                case['lr2021'] = []
            packets = []
            until = time.monotonic() + 120
            while time.monotonic() < until:
                s = line(x, .5)
                if s:
                    case['xiao'].append(s)
                if s.startswith('RXPACKET '):
                    packets.append(json.loads(s[9:]))
                if s.startswith('RXDECODE '):
                    case['decode'] = json.loads(s[9:])
                if s.startswith('RXPACKEND '):
                    break
            else:
                raise TimeoutError(case)
            case['packets'] = packets
            tx_ok = kind == 'no-rf' or any(s.startswith('TX_PUBLIC status=0 ') for s in case['lr2021'])
            case['passed'] = tx_ok and (
                any(v['crcOk'] and v['hex'] == data.hex() for v in packets)
                if case['expectAccept'] else not packets)
            report['cases'].append(case)
            del report['incompleteCase']
            print(len(report['cases']), kind, case['sf'], case['cr'], case['length'],
                  case['passed'], case.get('decode'), flush=True)
            if kind == 'no-payload-crc':
                command(r, 'CRC 1', 'CRC ')
            if kind == 'wrong-sync':
                command(r, 'SYNC 18', 'SYNC ')
        report['completed'] = True
    except Exception as error:
        report['fixtureException'] = repr(error)
        raise
    finally:
        x.close()
        r.close()
        report['finishedUtc'] = datetime.now(timezone.utc).isoformat()
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
