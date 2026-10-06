"""CRC-checked XIAO I/Q recordings with explicit sample-gap boundaries."""
import struct
import zlib
from pathlib import Path

import numpy as np

HEADER = struct.Struct('<4sIQHBBHBB')


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
