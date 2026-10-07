"""Scientific figures from retained native measurements, without invented data."""
import json
import math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 9,
                     'axes.spines.top': False, 'axes.spines.right': False,
                     'svg.fonttype': 'none', 'pdf.fonttype': 42})


def load(name):
    return json.loads((ROOT / 'evaluation/data' / name).read_text())


def save(fig, name):
    for ext in ('svg', 'png', 'pdf'):
        fig.savefig(ROOT / f'docs/assets/{name}.{ext}', dpi=220, facecolor='white')
    plt.close(fig)


def wilson(k, n):
    z = 1.959963984540054
    d = 1 + z*z/n
    m = (k/n + z*z/(2*n))/d
    r = z*math.sqrt(k/n*(1-k/n)/n + z*z/(4*n*n))/d
    return m-r, m+r


def levels():
    cases = load('native-levels-run2.json')['cases']
    values = [1, 2, 5, 10, 25, 50, 75, 100]
    fig, ax = plt.subplots(1, 2, figsize=(11.5, 4.7))
    for i, value in enumerate(values):
        rows = [c for c in cases if c['percent'] == value]
        n, k = len(rows), sum(c['passed'] for c in rows)
        if n != 4:
            raise ValueError('Unexpected amplitude cell size')
        low, high = wilson(k, n)
        ax[0].bar(i, k/n, width=.58, color='#237b85')
        ax[0].errorbar(i, k/n, yerr=[[k/n-low], [high-k/n]],
                       fmt='none', color='#263949', capsize=3)
        ax[0].text(i, 1.05, f'{k}/{n}', ha='center', fontsize=8)
        for j, c in enumerate(rows):
            if 'rssi' in c:
                ax[1].scatter(i+(j-1.5)*.045, c['rssi'], s=28,
                              marker='o' if c['passed'] else 'x',
                              color='#237b85' if c['passed'] else '#be594e')
    for a in ax:
        a.set_xticks(range(len(values)), values)
        a.set_xlabel('DAC amplitude (% of tested maximum; not dBm)')
        a.grid(axis='y', alpha=.18)
        a.set_axisbelow(True)
    ax[0].set_ylim(0, 1.15)
    ax[0].set_ylabel('Strict LR2021 CRC + exact bytes acceptance')
    ax[1].set_ylabel('LR2021 reported packet RSSI (dBm)')
    ax[0].set_title('A  32 fresh randomized transmissions', loc='left', fontweight='bold')
    ax[1].set_title('B  Reported RSSI; absent receptions omitted', loc='left', fontweight='bold')
    fig.subplots_adjust(left=.07, right=.98, top=.86, bottom=.31, wspace=.28)
    fig.text(.07, .06, 'SF7, CR4/5, 32 bytes, BW203.125 kHz, 2440.125 MHz; four attempts per level, no retries.\n'
             'Bars show actual counts; intervals are Wilson 95%, descriptive only for this stationary indoor pair.\n'
             'RSSI circles: accepted packets; crosses: rejected metadata/CRC. No calibrated radiated-power or sensitivity claim.',
             fontsize=8, color='#425465')
    save(fig, 'native-relative-levels')


def reception():
    r = load('native-rx-eight-bit.json')
    if not r['completed']:
        raise ValueError('Native matrix did not complete')
    cases = [c for c in r['cases'] if c['kind'] == 'packet']
    lengths = [1, 8, 32, 80, 255]
    heat = np.zeros((4, 5))
    fig, ax = plt.subplots(1, 2, figsize=(11.5, 4.9), gridspec_kw={'width_ratios': [1.1, 1]})
    for cr in range(1, 5):
        for j, n in enumerate(lengths):
            rows = [c for c in cases if c['sf'] == 7 and c['cr'] == cr and c['length'] == n]
            if len(rows) != 1:
                raise ValueError('Expected one fresh RF attempt per SF7 cell')
            k = sum(c['passed'] for c in rows)
            heat[cr-1, j] = k
            ax[0].text(j, cr-1, f'{k}/1', ha='center', va='center',
                       color='white' if k else '#263949', fontweight='bold')
    ax[0].imshow(heat, vmin=0, vmax=1, cmap='GnBu', aspect='auto')
    ax[0].set_xticks(range(5), lengths)
    ax[0].set_yticks(range(4), ['4/5', '4/6', '4/7', '4/8'])
    ax[0].set_xlabel('Payload length (bytes)')
    ax[0].set_ylabel('Coding rate')
    ax[0].set_title('A  SF7: full packet CRC + exact bytes', loc='left', fontweight='bold', pad=13)
    for sf in range(7, 13):
        rows = [c for c in cases if c['sf'] == sf]
        passed = [c for c in rows if c['passed']]
        times = [c['decode']['decodeUs']/1e6 for c in rows]
        for j, c in enumerate(rows):
            ax[1].scatter(sf+(j-(len(rows)-1)/2)*.013, c['decode']['decodeUs']/1e6,
                          color='#237b85' if c['passed'] else '#be594e',
                          marker='o' if c['passed'] else 'x', s=24)
        ax[1].text(sf, 100, f'{len(passed)}/{len(rows)}', ha='center', fontsize=8)
    ax[1].set_yscale('log')
    ax[1].set_ylim(.3, 140)
    ax[1].set_xticks(range(7, 13))
    ax[1].set_xlabel('Spreading factor (SF10–12: one 8-byte trial each)')
    ax[1].set_ylabel('On-device decode time after acquisition (s)')
    ax[1].grid(axis='y', alpha=.18)
    ax[1].set_title('B  Native latency and accepted/attempted counts', loc='left', fontweight='bold', pad=13)
    fig.subplots_adjust(left=.07, right=.98, top=.87, bottom=.32, wspace=.3)
    fig.text(.07, .055, 'Actual LR2021 RF → XIAO native C++ decoder; no PC decoding, no retries; one stationary indoor pair.\n'
             'SF7: four CRs × five lengths. SF8/9: CR4/5 and 4/8 × 8/32 bytes. SF10–12: CR4/5, 8 bytes.\n'
             '500 ms acquisition at SF7–9; 900 ms at SF10–12, then decoding blind time. Circles: accepted; crosses: rejected.\n'
             'Single-attempt cells demonstrate tested settings, not estimated reliability or receiver sensitivity.',
             fontsize=8, color='#425465')
    save(fig, 'native-reception')


if __name__ == '__main__':
    levels()
    reception()
    print('Saved native measurements as SVG/PNG/PDF')
