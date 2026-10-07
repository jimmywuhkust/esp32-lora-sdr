"""Install the public ESP-DSP component at the same pinned revision as capture."""
import pathlib, subprocess
PIN = 'a53a0756833c045311ea1d79a2badf495cdfde4c'
target = pathlib.Path(__file__).resolve().parent / 'components' / 'esp-dsp'
if not target.exists():
    target.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(['git', 'clone', '--filter=blob:none', '--no-checkout',
                    'https://github.com/espressif/esp-dsp.git', str(target)], check=True)
    subprocess.run(['git', '-C', str(target), 'checkout', '--detach', PIN], check=True)
actual = subprocess.check_output(['git', '-C', str(target), 'rev-parse', 'HEAD'], text=True).strip()
if actual != PIN:
    raise SystemExit(f'Existing dependency is not the required revision: {actual}')
print('ESP-DSP verified:', actual)
