"""Read-only proof of the autonomous Arduino pair, with optional LR2021 RX.

Flash the responder first and initiator last; run within its 10 s startup
delay. No serial input reaches either ESP32. All RF decoding is on the MCU.
"""
import argparse,hashlib,json,pathlib,re,time
from datetime import datetime,timezone
from verify_native_levels import openport,line,command
from verify_three_radios import strict_lr

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--ping',default='COM3');p.add_argument('--pong',default='COM5')
    p.add_argument('--lr',default='COM4');p.add_argument('--seconds',type=int,default=180)
    p.add_argument('--ping-image',type=pathlib.Path,required=True)
    p.add_argument('--pong-image',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error('use a new file; retain failed sessions')
    report=dict(startedUtc=datetime.now(timezone.utc).isoformat(),esp32SerialWrites=0,
                pcDecoder=False,hostIqUploaded=False,finished=False,lines=[],requests=[],results=[])
    for name,path in [('pingImage',a.ping_image),('pongImage',a.pong_image)]:
        raw=path.read_bytes();report[name]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
    ports={}
    try:
        ports={'ping':openport(a.ping,115200),'pong':openport(a.pong,115200)}
        if a.lr:
            ports['LR2021']=openport(a.lr,115200)
            for c in ('FREQ 2440125','BW 203125','PRE 16','SYNC 18','INV 0','CRC 1','SF 7'):
                reply,log=command(ports['LR2021'],c,c.split()[0])
                report['lines'] += [dict(board='LR2021',text=s) for s in log]
                assert reply==c+' status=0',reply
        end=time.monotonic()+a.seconds;finishedAt=None
        while time.monotonic()<end:
            for name,port in ports.items():
                s=line(port,.04)
                if not s:continue
                report['lines'].append(dict(board=name,text=s,elapsedSeconds=round(a.seconds-(end-time.monotonic()),3)))
                if name=='ping':
                    request=re.search(r'PING_SEND seq=(\d+) hex=([0-9a-f]{40})$',s)
                    result=re.search(r'PING_RESULT seq=(\d+) tx=(.*?) crc_exact_pong=([01])$',s)
                    if request:report['requests'].append(dict(sequence=int(request[1]),hex=request[2]))
                    if result:
                        report['results'].append(dict(sequence=int(result[1]),tx=result[2],crcExactPong=result[3]=='1'))
                        print(s,flush=True)
                    if 'PING_FINISHED ' in s:report['finished']=True;finishedAt=time.monotonic()
            if finishedAt and time.monotonic()-finishedAt>5:break
        # Match the responder's decoded full request to the actual initiator
        # bytes. Missing USB lines stay missing, even if RF reception passed.
        for req in report['requests']:
            wanted='ARDUINO_RX crc_ok=1 bytes=20 sf=7 cr=4 hex='+req['hex']
            req['responderCrcExact']=any(v['board']=='pong' and v['text'].endswith(wanted) for v in report['lines'])
            observed=[v['text'] for v in report['lines'] if v['board']=='LR2021']
            req['independentRequestCrcExact']=strict_lr(observed,bytes.fromhex(req['hex']))
            req['independentReplyCrcExact']=strict_lr(observed,bytes.fromhex('504f4e47'+req['hex'][8:]))
        report['requestCopiesObserved']=sum(v['board']=='ping' and v['text'].startswith('PING_COPY ') for v in report['lines'])
        report['replyCopiesObserved']=sum(v['board']=='pong' and v['text'].startswith('PONG_SEND ') for v in report['lines'])
    except BaseException as e:
        report['error']=repr(e);raise
    finally:
        for port in ports.values():port.close()
        report['finishedUtc']=datetime.now(timezone.utc).isoformat()
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8')

if __name__=='__main__':main()
