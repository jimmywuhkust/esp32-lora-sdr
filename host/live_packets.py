"""Public LR2021 TX -> XIAO I/Q -> PC full CRC decode.

Separate bounded windows, one fresh packet each; no RF retries. Expected
bytes are compared only after decoding, never supplied to the decoder.
Requires the optional aerolink-hf-tx companion and the IDF IQ capture image.
"""
import argparse, hashlib, json, random, threading, time
from datetime import datetime, timezone
from pathlib import Path
import serial
from lora_packet_iq import capture, command, contiguous_runs, frames_from_file
from lora_packet_decoder import Decoder

def open_port(name,baud):
    p=serial.Serial(port=None,baudrate=baud,timeout=3,write_timeout=3)
    p.dtr=p.rts=False;p.port=name;p.open();p.reset_input_buffer();return p

def main():
    a=argparse.ArgumentParser()
    a.add_argument('--xiao-port',required=True);a.add_argument('--radio-port',required=True)
    a.add_argument('--xiao-image',type=Path);a.add_argument('--radio-image',type=Path)
    a.add_argument('--xiao-source',type=Path);a.add_argument('--radio-source',type=Path)
    a.add_argument('--sfs',default='7,8,9');a.add_argument('--repeats',type=int,default=3)
    a.add_argument('--lengths',default='32');a.add_argument('--crs',default='4')
    a.add_argument('--seed',type=int,default=2026100721)
    a.add_argument('--window-ms',type=int,default=500)
    a.add_argument('--output',type=Path,required=True)
    cfg=a.parse_args();sfs=[int(s) for s in cfg.sfs.split(',')]
    lengths=[int(s) for s in cfg.lengths.split(',')];crs=[int(s) for s in cfg.crs.split(',')]
    if not sfs or not crs or not lengths or any(s not in (7,8,9) for s in sfs) or any(s not in (1,2,3,4) for s in crs) or any(not 1<=s<=32 for s in lengths) or not 1<=cfg.repeats<=10:raise ValueError('Invalid bounded fixture settings')
    if not 350<=cfg.window_ms<=1000:raise ValueError('Capture window must be 350–1000 ms')
    cfg.output.mkdir(parents=True,exist_ok=False)
    images=[p for p in (cfg.xiao_image,cfg.radio_image) if p is not None]
    result=dict(startedUtc=datetime.now(timezone.utc).isoformat(),completed=False,
        direction='LR2021 HF TX -> XIAO I/Q -> PC',nativeArduinoRx=False,
        seed=cfg.seed,windowMs=cfg.window_ms,frequencyHz=2440125000,bandwidthHz=203125,
        syncWord=18,preamble=16,requestedChipPowerDbm=-12,calibratedRadiatedPower=False,
        imageSha256={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in images},cases=[])
    result['configuration']=dict(sfs=sfs,crs=crs,lengths=lengths,repeats=cfg.repeats,
        xiaoPort=cfg.xiao_port,radioPort=cfg.radio_port,rfRetries=0)
    result['sourceSha256']={}
    for name,directory in (('xiao',cfg.xiao_source),('radio',cfg.radio_source)):
        if directory is None:continue
        selected={str(p.relative_to(directory)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in directory.rglob('*') if p.is_file()
            and not any(part.startswith('.') or part.startswith('build') or part=='components' for part in p.relative_to(directory).parts)
            and (p.suffix in ('.c','.cpp','.h','.S','.ld','.lf','.ini') or p.name in ('CMakeLists.txt','sdkconfig.defaults','sdkconfig.psram','Kconfig.projbuild'))}
        result['sourceSha256'][name]=selected
    xiao=radio=None;rng=random.Random(cfg.seed)
    pending={}
    def line(port,timeout=5):
        until=time.monotonic()+timeout;data=pending.setdefault(port,bytearray())
        while time.monotonic()<until:
            data.extend(port.readline())
            if b'\n' in data:
                end=data.index(10)+1;reply=bytes(data[:end]);del data[:end]
                return reply.decode(errors='replace').strip()
        # A serial timeout can split a line: retain bytes for the next read.
        # Partial TX diagnostics must never count as a complete acknowledgement.
        return ''
    try:
        xiao=open_port(cfg.xiao_port,2000000)
        if hasattr(xiao,'set_buffer_size'):xiao.set_buffer_size(rx_size=1048576,tx_size=65536)
        radio=open_port(cfg.radio_port,115200);radio.timeout=.1
        result['xiaoInfo']=command(xiao,'INFO');radio.write(b'INFO\n');result['radioInfo']=line(radio)
        if not result['radioInfo'].startswith('LR2021_PUBLIC status=0 ready=1'):raise RuntimeError(result['radioInfo'])
        for cmd in ['FREQ 2440125','BW 203125','PRE 16','SYNC 18','INV 0']:
            radio.write((cmd+'\n').encode());reply=line(radio)
            if reply!=cmd+' status=0':raise RuntimeError(reply)
        for block in range(cfg.repeats):
            tuples=[(sf,cr,n) for sf in sfs for cr in crs for n in lengths];rng.shuffle(tuples)
            for sf,cr,n in tuples:
                data=rng.randbytes(n);number=len(result['cases'])+1
                case=dict(sf=sf,cr=cr,length=n,block=block,expected=data.hex(),startedUtc=datetime.now(timezone.utc).isoformat())
                (cfg.output/f'input-{number:03d}.json').write_text(json.dumps(case,indent=2),encoding='utf-8')
                started=threading.Event();errors=[];tx=[];diagnostics=[];txTimes=[];before=time.monotonic()
                def transmit():
                    if not started.wait(5):errors.append('Capture did not start');return
                    time.sleep(.05)
                    try:
                        txTimes.append(time.monotonic()-before)
                        radio.reset_input_buffer();pending.pop(radio,None);radio.write(f'TX {sf} {cr} {data.hex()}\n'.encode())
                        deadline=time.monotonic()+3
                        while time.monotonic()<deadline:
                            reply=line(radio,.2)
                            if reply.startswith('TX_PUBLIC'):tx.append(reply);break
                            if reply:diagnostics.append(reply)
                        if not tx:errors.append('No TX_PUBLIC reply within 3s')
                        if tx:diagnostics.append(line(radio,1))
                    except Exception as error:errors.append(repr(error))
                worker=threading.Thread(target=transmit)
                worker.start();path=cfg.output/f'window-{number:03d}.iqs'
                try:case['capture']=capture(xiao,path,cfg.window_ms/1000,4,9,2440.125,firstFrame=started)
                except BaseException as error:
                    case.update(passed=False,captureError=repr(error),partialCapture=True)
                    if path.exists():case['partialIqSha256']=hashlib.sha256(path.read_bytes()).hexdigest()
                    result['cases'].append(case)
                    raise
                finally:
                    worker.join(timeout=6)
                    if 'captureError' in case:
                        case.update(txLines=tx,errors=errors,txStartAfterHostCaptureCallSeconds=txTimes,
                            txDiagnostics=diagnostics,trigger='first validated IQ frame + 50ms')
                        (cfg.output/f'case-{number:03d}.json').write_text(json.dumps(case,indent=2),encoding='utf-8')
                if worker.is_alive():errors.append('TX worker did not finish')
                case.update(txLines=tx,errors=errors,txStartAfterHostCaptureCallSeconds=txTimes,
                    txDiagnostics=diagnostics,
                    trigger='first validated IQ frame + 50ms',iqSha256=hashlib.sha256(path.read_bytes()).hexdigest())
                runs=list(contiguous_runs(frames_from_file(path)));decoder=Decoder(sf=sf)
                packets=[]
                for index,iq in runs:packets.extend(decoder.decode(iq,index))
                case.update(packets=packets,decoderStats=decoder.stats.copy(),contiguousRuns=len(runs))
                case['passed']=not errors and tx==[f'TX_PUBLIC status=0 sf={sf} cr={cr} bytes={len(data)} hex={data.hex()}'] and any(p['crcOk'] and p['hex']==data.hex() for p in packets)
                result['cases'].append(case)
                (cfg.output/f'case-{number:03d}.json').write_text(json.dumps(case,indent=2),encoding='utf-8')
                print(number,sf,cr,n,case['passed'],'packets',len(packets),'coverage',case['capture']['coverage'],flush=True)
                if errors or not tx or not tx[0].startswith('TX_PUBLIC status=0'):raise RuntimeError('TX command or local completion failed')
        result['completed']=True
    except BaseException as error:result['error']=repr(error);raise
    finally:
        if xiao:xiao.close()
        if radio:radio.close()
        result['finishedUtc']=datetime.now(timezone.utc).isoformat()
        (cfg.output/'report.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(cfg.output.resolve(),flush=True)

if __name__=='__main__':main()
