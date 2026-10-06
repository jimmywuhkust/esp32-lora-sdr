"""Measured grant/readback follow-up and independent bounded RF matrix."""
import json, math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parent.parent
bank=json.loads((ROOT/'evaluation/data/research/playing-bank-grant.json').read_text())
rf=json.loads((ROOT/'evaluation/data/research/grant-stream-sf7-matrix.json').read_text())
if not bank['completed'] or not rf['completed']:raise ValueError('Incomplete experiment')
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.spines.top':False,
    'axes.spines.right':False,'svg.fonttype':'none','pdf.fonttype':42,'axes.titleweight':'bold'})
fig,axes=plt.subplots(1,3,figsize=(12.6,4.4),gridspec_kw={'width_ratios':[1,1,1.25]})
labels=['Idle','Playing','Grant released']
for i,cmd in enumerate(['BANKIDLE','BANKPLAY','BANKGRANT']):
    rows=[c for c in bank['cases'] if c['command']==cmd]
    values=[100*c['wrongWords']/16384 for c in rows]
    axes[0].bar(i,np.mean(values),width=.58,color=['#237b85','#ab593e','#237b85'][i])
    axes[0].scatter([i]*len(values),values,color='#203246',s=20,zorder=3)
    axes[0].annotate(f'{rows[0]["wrongWords"]:,}/16,384',
        (i,max(values)),xytext=(0,9),textcoords='offset points',ha='center',fontsize=8)
    timing=[c['maxCopyCycles'] for c in rows]
    axes[1].scatter([i]*len(timing),timing,color='#237b85',s=25)
    axes[1].annotate(str(max(timing)),(i,max(timing)),xytext=(0,-20),textcoords='offset points',ha='center',fontsize=8)
axes[0].set_ylim(-4,118);axes[0].set_ylabel('Wrong SRAM words (%)')
axes[0].set_title('A  Stored-word integrity',loc='left',pad=14)
axes[1].set_ylim(1110,1660);axes[1].set_ylabel('256-word copy (CPU cycles)')
axes[1].axhline(1536,ls='--',color='#ab593e',lw=1)
axes[1].text(1,1570,'Reader budget: 1,536',ha='center',fontsize=8,color='#ab593e')
axes[1].set_title('B  Copy budget',loc='left',pad=14)
for a in axes[:2]:
    a.set_xticks(range(3),labels,rotation=18);a.grid(axis='y',alpha=.15);a.set_axisbelow(True)
lengths=[1,8,32,80];heat=np.zeros((4,4))
for i,cr in enumerate(range(1,5)):
    for j,length in enumerate(lengths):
        rows=[c for c in rf['cases'] if c['cr']==cr and c['length']==length]
        k=sum(c['passed'] for c in rows);n=len(rows)
        if n!=3:raise ValueError('Expected three trials per tuple')
        heat[i,j]=k/n
        axes[2].text(j,i,f'{k}/{n}',ha='center',va='center',color='white',fontweight='bold')
axes[2].imshow(heat,vmin=0,vmax=1,cmap='YlGnBu',aspect='auto')
axes[2].set_xticks(range(4),lengths);axes[2].set_yticks(range(4),['4/5','4/6','4/7','4/8'])
axes[2].set_xlabel('Payload (bytes)');axes[2].set_ylabel('Coding rate')
axes[2].set_title('C  Independent SF7 RF packets',loc='left',pad=14)
k=sum(c['passed'] for c in rf['cases']);n=len(rf['cases']);z=1.959963984540054
p=k/n;d=1+z*z/n;m=(p+z*z/(2*n))/d;r=z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))/d
fig.subplots_adjust(left=.065,right=.98,top=.8,bottom=.29,wspace=.43)
fig.text(.065,.065,'A–B: 3 alternating deterministic checks, no RF keying; repeated readbacks are not independent statistical trials.\n'
    f'C: {k}/{n} fresh randomized packets; SF7/BW203.125 kHz, one stationary board pair; descriptive Wilson 95% {(m-r)*100:.2f}–{(m+r)*100:.2f}%.\n'
    'CRC + exact full bytes required. SF8 remains unsuccessful. Grant release does not establish gapless analog RF output.',fontsize=8,color='#425465')
for ext in ['svg','png','pdf']:fig.savefig(ROOT/f'docs/assets/grant-results.{ext}',dpi=240,facecolor='white')
plt.close(fig)
print(f'{k}/{n}; Wilson {(m-r)*100:.5f}–{(m+r)*100:.5f}%')
