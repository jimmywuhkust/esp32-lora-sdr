"""Fetch the pinned public ESP-DSP source; never replace an existing checkout."""
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parent
PIN='a53a0756833c045311ea1d79a2badf495cdfde4c'
destination=ROOT/'components/esp-dsp'
if not destination.exists():
    destination.parent.mkdir(exist_ok=True)
    subprocess.run(['git','clone','--filter=blob:none','--no-checkout',
                    'https://github.com/espressif/esp-dsp.git',str(destination)],check=True)
    subprocess.run(['git','-C',str(destination),'checkout','--detach',PIN],check=True)
actual=subprocess.check_output(['git','-C',str(destination),'rev-parse','HEAD'],text=True).strip()
if actual!=PIN:raise SystemExit(f'Existing ESP-DSP checkout differs from required pin: {actual}')
print('ESP-DSP verified:',actual)
