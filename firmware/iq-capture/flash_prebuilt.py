"""Verify pinned generated artifacts before calling esptool; no flash erase-all."""
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--variant',choices=['default','psram','native'],default='default')
    parser.add_argument('--port',help='XIAO native USB serial port, e.g. COM3')
    parser.add_argument('--check-only',action='store_true')
    args=parser.parse_args()
    directory=ROOT/'prebuilt'/args.variant
    manifest=json.loads((directory/'manifest.json').read_text(encoding='utf-8'))
    commands=[sys.executable,'-m','esptool','--chip','esp32s3','--port',args.port or '',
        '--baud','460800','write-flash','--flash-mode','dio','--flash-size','8MB','--flash-freq','80m']
    for item in manifest['images']:
        file=directory/item['file'];raw=file.read_bytes()
        if len(raw)!=item['bytes'] or hashlib.sha256(raw).hexdigest()!=item['sha256']:
            raise ValueError('Artifact checksum mismatch: '+str(file))
        commands.extend([item['offset'],str(file)])
    print(f"Verified {args.variant}: {len(manifest['images'])} generated images; source pins in manifest.")
    if args.check_only:return
    if not args.port:parser.error('--port is required unless --check-only')
    subprocess.run(commands,check=True)
if __name__=='__main__':main()
