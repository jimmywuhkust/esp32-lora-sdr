"""Actual strict receiver audit; no reclassification of ambiguous events."""
import json,math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parent.parent
r=json.loads((ROOT/'evaluation/data/public-receiver-final-irq.json').read_text())
if not r['completed'] or len(r['cases'])!=240:raise ValueError('Requires completed actual matrix')
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.spines.top':False,'axes.spines.right':False,'svg.fonttype':'none','pdf.fonttype':42})
fig,axes=plt.subplots(1,2,figsize=(11.8,5.2),gridspec_kw={'width_ratios':[1.25,1]})
lengths=[1,8,32,80,128,255];heat=np.empty((4,6))
for cr in range(1,5):
    for j,n in enumerate(lengths):
        cases=[c for c in r['cases'] if c['cr']==cr and c['length']==n]
        if len(cases)!=10:raise ValueError('Unequal cell size')
        k=sum(c['passed'] for c in cases);heat[cr-1,j]=k/10
        axes[0].text(j,cr-1,f'{k}/10',ha='center',va='center',color='white',fontweight='bold')
axes[0].imshow(heat,vmin=.5,vmax=1,cmap='YlGnBu',aspect='auto')
axes[0].set_xticks(range(6),lengths);axes[0].set_yticks(range(4),['4/5','4/6','4/7','4/8'])
axes[0].set_xlabel('Payload length (bytes)');axes[0].set_ylabel('Coding rate')
axes[0].set_title('A  No-error IRQ + full CRC + exact bytes',loc='left',fontweight='bold',pad=15)
k=sum(c['passed'] for c in r['cases']);n=len(r['cases']);z=1.959963984540054;p=k/n;den=1+z*z/n;m=(p+z*z/2/n)/den;rad=z*math.sqrt(p*(1-p)/n+z*z/4/n/n)/den
axes[1].bar([0,1],[k,240],width=.55,color=['#237b85','#7793a8'])
axes[1].errorbar(0,k,yerr=[[k-n*(m-rad)],[n*(m+rad)-k]],fmt='none',color='#203246',capsize=4)
axes[1].text(0,k+17,f'{k}/240',ha='center',fontweight='bold');axes[1].text(1,252,'240/240',ha='center',fontweight='bold')
axes[1].set_xticks([0,1],['Strict no-error IRQ\nacceptance','Exact payload bytes\nregardless of header event']);axes[1].set_ylim(0,278);axes[1].set_ylabel('Packets');axes[1].grid(axis='y',alpha=.15);axes[1].set_axisbelow(True)
axes[1].set_title('B  Separate evidence criteria',loc='left',fontweight='bold',pad=15)
fig.subplots_adjust(left=.06,right=.97,top=.86,bottom=.32,wspace=.28)
fig.text(.06,.065,'Actual fresh SF7 / BW203.125 kHz bench, 2440.125 MHz; four CRs × six lengths × ten shuffled blocks; no RF retries.\n16 exact payloads had simultaneous header-valid + latched header-CRC-error events; all were conservatively rejected.\nThe payload CRC error bit was absent. Event attribution is unresolved; these are not 16 missing or payload-corrupt packets.\nWilson 95% interval for strict acceptance: 89.45–95.86%, descriptive only; one stationary indoor pair, uncalibrated RF power.',fontsize=8,color='#425465')
for ext in ('svg','png','pdf'):fig.savefig(ROOT/f'docs/assets/final-irq-results.{ext}',dpi=240,facecolor='white')
print('Saved actual 240-case strict IRQ figure')
