"""CRC-checked XIAO I/Q recordings with explicit sample-gap boundaries."""
import struct
import zlib
from pathlib import Path

import numpy as np

HEADER = struct.Struct('<4sIQHBBHBB')


def exact(port, count):
    data=bytearray()
    while len(data)<count:
        chunk=port.read(count-len(data))
        if not chunk:
            raise TimeoutError(f'Short IQ transfer: {len(data)}/{count}')
        data.extend(chunk)
    return bytes(data)


def command(port, text):
    port.write((text+'\n').encode('ascii'))
    reply=port.readline().decode('ascii').strip()
    if not reply or reply.startswith('ERR'):
        raise RuntimeError(f'{text}: {reply}')
    return reply


def decode_frame(wire):
    if len(wire)<28:
        raise ValueError('Short IQ frame')
    magic,seq,index,n,bits,flags,dec,gain,shift=HEADER.unpack_from(wire)
    if magic!=b'IQS1' or bits not in (4,8,16) or not 0<n<=1024 or dec<=0 or flags&~3 or len(wire)!=28+n*bits//4:
        raise ValueError('Invalid IQ frame fields')
    if zlib.crc32(wire[:-4])!=int.from_bytes(wire[-4:],'little'):
        raise ValueError('IQ transport CRC failure')
    body=wire[24:-4]
    if bits==4:
        b=np.frombuffer(body,dtype=np.uint8)
        i=((b&15).astype(np.int16)+8)%16-8
        q=((b>>4).astype(np.int16)+8)%16-8
        iq=i.astype(np.float32)+1j*q.astype(np.float32)
    else:
        b=np.frombuffer(body,dtype=np.int8 if bits==8 else '<i2').reshape(-1,2)
        iq=b[:,0].astype(np.float32)+1j*b[:,1].astype(np.float32)
    return dict(sequence=seq,index=index,samples=n,bits=bits,flags=flags,decimation=dec,gain=gain,shift=shift,iq=iq)


def frames_from_file(path):
    raw=Path(path).read_bytes()
    pos=0
    while pos<len(raw):
        if len(raw)-pos<24:
            raise ValueError('Truncated IQ recording header')
        _,_,_,n,bits,*_=HEADER.unpack_from(raw,pos)
        size=28+n*bits//4
        frame=decode_frame(raw[pos:pos+size])
        yield frame
        pos+=size


def contiguous_runs(frames):
    start=None
    parts=[]
    previous=None
    for f in frames:
        if previous is not None:
            if f['index']<previous['index']+previous['samples'] or f['sequence']<=previous['sequence']:
                raise ValueError('IQ indices moved backwards')
            gap=f['index']!=previous['index']+previous['samples'] or f['sequence']!=previous['sequence']+1 or bool(f['flags']&3)
            if gap and parts:
                yield start,np.concatenate(parts)
                start=None; parts=[]
        if start is None:
            start=f['index']
        parts.append(f['iq'])
        previous=f
    if parts:
        yield start,np.concatenate(parts)


def capture(port,path,seconds=3,bits=4,shift=9,frequency=2440.125,started=None,stop=None,firstFrame=None):
    mhz=int(frequency)
    for text in (f'FOFS {round((frequency-mhz)*1000)}',f'FREQ {mhz-4}','BANDWIDTH 0','GAIN HARDWARE','DUAL 1'):
        if command(port,text)!='OK':
            raise RuntimeError('Cannot apply '+text)
    reply=command(port,f'IQS {round(seconds*1000)} 64 {bits} 6 {shift} 2')
    if not reply.startswith(f'IQS 16000000 64 {bits} {shift} 2 '):
        raise RuntimeError('Unexpected IQ start response: '+reply)
    if started:
        started.set()
    stored=bytearray()
    count=0
    previous=None
    frames=0
    lost=0
    stopped=False
    while True:
        if stop and stop() and not stopped:
            port.write(b'\n')
            stopped=True
        prefix=exact(port,4)
        if prefix==b'IQSE':
            fields=(prefix+port.readline()).decode('ascii').strip().split()
            if len(fields)!=13 or fields[0]!='IQSEND' or fields[1]!='0':
                # Preserve the validated prefix even when the hardware aborts.
                # It is explicitly partial and must not pass a full-window test.
                Path(path).write_bytes(stored)
                raise RuntimeError('Bad IQ completion: '+' '.join(fields))
            end=list(map(int,fields[1:]))
            break
        if prefix!=b'IQS1':
            raise ValueError('Lost IQ frame boundary')
        h=prefix+exact(port,20)
        _,_,_,n,b,*_=HEADER.unpack(h)
        if b not in (4,8,16) or not 0<n<=1024:
            raise ValueError('Invalid IQ frame size')
        wire=h+exact(port,n*b//4+4)
        f=decode_frame(wire)
        if f['bits']!=bits or f['decimation']!=64:
            raise ValueError('IQ configuration changed mid-stream')
        if firstFrame is not None and frames==0:
            # IQS acknowledgement precedes prepare_rx and is not an RF-ready
            # indication. A validated first IQ frame confirms acquisition.
            firstFrame.set()
        if previous is not None:
            lost+=max(0,f['index']-previous)
        previous=f['index']+n
        count+=n;frames+=1
        stored.extend(wire)
    Path(path).write_bytes(stored)
    command(port,'FOFS 0')
    rf_samples=end[3]//64
    return dict(sampleRate=250000,bits=bits,shift=shift,frames=frames,samples=count,gapSamples=max(lost,rf_samples-count),rfSamples=rf_samples,coverage=100*count/rf_samples if rf_samples else 0,outputDrops=end[8],abandonedUnits=end[9],verifiedDuplicateRfPairsTrimmed=end[1],end=end,file=str(path))
