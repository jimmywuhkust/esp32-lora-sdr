"""Scientific settings matrix and digital SRAM readback; real saved data only."""
import json, math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'docs/assets'
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.spines.top':False,
    'axes.spines.right':False,'svg.fonttype':'none','pdf.fonttype':42,'axes.titleweight':'bold'})
def save(fig,name):
    for suffix in ('svg','pdf','png'):fig.savefig(OUT/f'{name}.{suffix}',dpi=240,facecolor='white')
    path=OUT/f'{name}.svg'
    path.write_text('\n'.join(line.rstrip() for line in path.read_text(encoding='utf-8').splitlines())+'\n',encoding='utf-8')
    plt.close(fig)
def wilson(k,n):
    z=1.959963984540054;p=k/n;d=1+z*z/n
    m=(p+z*z/(2*n))/d;r=z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))/d
    return m-r,m+r

bank=json.loads((ROOT/'evaluation/data/research/stream-lut-playing-bank.json').read_text())
if not bank['completed']:raise ValueError('Readback experiment incomplete')
fig,axes=plt.subplots(1,2,figsize=(10.4,3.8))
for i,command in enumerate(['BANKIDLE','BANKPLAY']):
    rows=[c for c in bank['cases'] if c['command']==command]
    values=[100*c['wrongWords']/16384 for c in rows]
    axes[0].bar(i,np.mean(values),width=.55,color=['#237b85','#ab593e'][i])
    axes[0].scatter([i]*len(values),values,color='#203246',s=23,zorder=3)
    axes[0].annotate(f'{rows[0]["wrongWords"]:,}/16,384\neach of 3 checks',(i,max(values)),
        xytext=(0,10),textcoords='offset points',ha='center',fontsize=8)
    timings=[c['maxCopyCycles'] for c in rows]
    axes[1].scatter([i]*len(timings),timings,color='#237b85',s=35)
    axes[1].annotate(f'{min(timings)}–{max(timings)}',(i,max(timings)),xytext=(0,-23),textcoords='offset points',ha='center',fontsize=8)
axes[0].set_ylim(-4,119);axes[0].set_ylabel('Wrong SRAM words after write (%)')
axes[0].set_title('A  Playing-bank integrity fails',loc='left',pad=14)
axes[1].axhline(1536,color='#ab593e',ls='--',lw=1)
axes[1].text(.5,1549,'40 MS/s reader budget = 1,536',ha='center',fontsize=8,color='#ab593e')
axes[1].set_ylim(1080,1640);axes[1].set_ylabel('Maximum 256-word copy (CPU cycles)')
axes[1].set_title('B  Copying speed still meets budget',loc='left',pad=14)
for axis in axes:
    axis.set_xticks([0,1],['Engine idle','Engine playing']);axis.set_xlim(-.6,1.6);axis.grid(axis='y',alpha=.15);axis.set_axisbelow(True)
fig.subplots_adjust(left=.09,right=.98,top=.8,bottom=.28,wspace=.4)
fig.text(.09,.04,'XIAO ESP32-S3 · 240 MHz CPU · identical pattern and writer · 3 alternating pairs · no RF keying\n'
    'Digital SRAM readback, not an RF spectrum or packet-delivery result. Deterministic repeated checks are not independent statistical trials.',fontsize=8,color='#425465')
save(fig,'playing-bank-results')

path=ROOT/'evaluation/data/phy-settings-matrix.json'
if path.exists():
    data=json.loads(path.read_text(encoding='utf-8'))
    if not data['completed']:raise ValueError('RF settings matrix incomplete')
    cases=data['cases'];frequencies=sorted({c['frequencyHz'] for c in cases});preambles=sorted({c['preamble'] for c in cases})
    fig,axes=plt.subplots(1,2,figsize=(10.4,4.0))
    for axis,sync in zip(axes,[18,52]):
        heat=np.zeros((len(frequencies),len(preambles)))
        for i,freq in enumerate(frequencies):
            for j,pre in enumerate(preambles):
                rows=[c for c in cases if c['frequencyHz']==freq and c['preamble']==pre and c['syncWord']==sync]
                k=sum(c['passed'] for c in rows);n=len(rows);heat[i,j]=k/n
                axis.text(j,i,f'{k}/{n}',ha='center',va='center',color='white',fontweight='bold')
        axis.imshow(heat,vmin=.5,vmax=1,cmap='YlGnBu',aspect='auto')
        axis.set_xticks(range(len(preambles)),preambles);axis.set_yticks(range(len(frequencies)),[f'{f/1e6:.3f}' for f in frequencies])
        axis.set_xlabel('Preamble (symbols)');axis.set_ylabel('RF channel (MHz)')
        axis.set_title(f'{"A" if sync==18 else "B"}  Sync word 0x{sync:02X}',loc='left',pad=14)
    k=sum(c['passed'] for c in cases);n=len(cases);lo,hi=wilson(k,n)
    fig.subplots_adjust(left=.09,right=.98,top=.82,bottom=.31,wspace=.36)
    fig.text(.09,.06,f'SF7 · BW203.125 kHz · CR4/8 · 32 fresh random bytes · 48 settings × 3 shuffled blocks = {n} trials\n'
        f'Each cell includes both matched IQ polarities. {k}/{n}; descriptive Wilson 95% interval {100*lo:.2f}–{100*hi:.2f}%. One stationary board pair.',fontsize=8,color='#425465')
    save(fig,'phy-settings-results')
    print(f'{k}/{n}; Wilson 95% {100*lo:.5f}–{100*hi:.5f}%')
