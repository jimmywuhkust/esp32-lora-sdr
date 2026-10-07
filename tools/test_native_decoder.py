"""Compile the same C++ decoder used by ESP32; test real RF and negatives."""
import argparse,hashlib,json,pathlib,subprocess,sys,tempfile
import numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from lora_packet_iq import contiguous_runs,frames_from_file

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--compiler',default='g++');parser.add_argument('--zig',action='store_true',help='Use the compiler as zig c++');args=parser.parse_args()
    with tempfile.TemporaryDirectory() as temp:
        work=pathlib.Path(temp);exe=work/('native.exe' if sys.platform=='win32' else 'native')
        subprocess.run([args.compiler,*(['c++'] if args.zig else []),'-O2','-std=c++11','-I',str(ROOT/'src'),
            *map(str,[ROOT/'tools/native_decode.cpp',ROOT/'src/LoRaSDR.cpp',ROOT/'src/PacketDecoder.cpp',ROOT/'src/IQDecoder.cpp']),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
        api=work/('api.exe' if sys.platform=='win32' else 'api')
        subprocess.run([args.compiler,*(['c++'] if args.zig else []),'-O2','-std=c++11','-I',str(ROOT/'src'),
            str(ROOT/'tools/test_library_api.cpp'),'-o',str(api)],check=True)
        subprocess.run([str(api)],check=True)
        def decode(iq,sf):
            path=work/'iq16.bin';np.stack((iq.real,iq.imag),axis=1).astype('<i2').tofile(path)
            result=subprocess.run([str(exe),str(path),str(sf)],check=True,capture_output=True,text=True)
            return [json.loads(s) for s in result.stdout.splitlines() if s.startswith('{')]
        for sf in (7,8,9):
            _,iq=next(contiguous_runs(frames_from_file(ROOT/'host/samples'/f'lr2021-sf{sf}-window.iqs')))
            manifest=json.loads((ROOT/'host/samples'/('manifest.json' if sf==7 else f'lr2021-sf{sf}-manifest.json')).read_text())
            packets=decode(iq,sf)
            assert len(packets)==1 and packets[0]['crcOk'],(sf,packets)
            # Independent expected bytes are used only after native decoding.
            expected=manifest['expectedPayloadHex']
            assert packets[0]['hex']==expected,(sf,packets)
            assert not decode(iq[:max(2000,len(iq)//10)],sf),'truncated prefix accepted'
            assert not decode(np.zeros(12000,dtype=np.complex64),sf),'zero input accepted'
            noise=np.random.default_rng(sf).integers(-8,8,(12000,2))
            assert not decode(noise[:,0]+1j*noise[:,1],sf),'noise accepted'
            print(f'SF{sf}: real full packet + truncation/zero/noise negative tests passed')
        for fixture in json.loads((ROOT/'host/samples/esp32-peer-manifest.json').read_text()):
            raw=(ROOT/'host/samples'/fixture['file']).read_bytes()
            assert len(raw)==fixture['bytes'] and hashlib.sha256(raw).hexdigest()==fixture['sha256']
            iq=np.frombuffer(raw,dtype='<i2').reshape(-1,2)
            packets=decode(iq[:,0]+1j*iq[:,1],7)
            assert len(packets)==1 and packets[0]['crcOk'],packets
            assert packets[0]['hex']==fixture['expectedHex'],packets
            print(f"{fixture['file']}: saved ESP32 RF full CRC packet recovered (soft={packets[0]['softDecoded']}, offline regression)")
if __name__=='__main__':main()
