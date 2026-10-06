"""Render real-RF results with Wilson intervals; no hardware or fabricated rows."""
import json, math
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE=Path(__file__).resolve().parent
DATA=HERE/'data';OUT=HERE.parent/'docs/assets';OUT.mkdir(parents=True,exist_ok=True)
def read(name):return json.loads((DATA/name).read_text(encoding='utf-8'))
def wilson(k,n):
    z=1.959963984540054;p=k/n;d=1+z*z/n
    middle=(p+z*z/(2*n))/d
    radius=z*math.sqrt(p*(1-p)/n+z*z/(4*n*n))/d
    return middle-radius,middle+radius

plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.spines.top':False,
    'axes.spines.right':False,'svg.fonttype':'none','pdf.fonttype':42,'axes.titleweight':'bold'})
fig,ax=plt.subplots(1,3,figsize=(12.2,3.8),gridspec_kw={'width_ratios':[1.1,1.25,1]})
names=['PLL\nbaseline','IDF DAC\nwindows','Arduino\npre-fix','Arduino\nfixed']
files=['pll-baseline.json','idf-dac-baseline.json','native-before-timing-fix.json','native-after-timing-fix.json']
counts=[(sum(c['passed'] for c in read(f)['cases']),len(read(f)['cases'])) for f in files]
rates=np.array([k/n for k,n in counts]);ci=np.array([wilson(k,n) for k,n in counts])
ax[0].bar(range(4),rates,color=['#657589','#289cb0','#ccb46d','#217e62'],width=.62)
ax[0].errorbar(range(4),rates,yerr=[rates-ci[:,0],ci[:,1]-rates],fmt='none',ecolor='#24394a',capsize=3)
for j,(k,n) in enumerate(counts):ax[0].text(j,1.065,f'{k}/{n}',ha='center',fontsize=9)
ax[0].set_xticks(range(4),names);ax[0].set_ylim(0,1.15);ax[0].set_yticks([0,.25,.5,.75,1],['0','25','50','75','100'])
ax[0].set_ylabel('Exact-payload, CRC-valid reception (%)');ax[0].set_title('A  Same 26 baseline payloads',loc='left',pad=17)
ax[0].grid(axis='y',alpha=.15);ax[0].set_axisbelow(True)

matrix=read('native-matrix-basic.json');lengths=[1,8,32,80,128,250]
heat=np.empty((4,6));labels=[]
for cr in range(1,5):
    row=[]
    for j,length in enumerate(lengths):
        cases=[c for c in matrix['cases'] if c['cr']==cr and c['length']==length]
        k=sum(c['passed'] for c in cases);n=len(cases);heat[cr-1,j]=k/n;row.append((k,n))
    labels.append(row)
ax[1].imshow(heat,vmin=.5,vmax=1,cmap='YlGnBu',aspect='auto')
for i in range(4):
    for j in range(6):
        k,n=labels[i][j];ax[1].text(j,i,f'{k}/{n}',ha='center',va='center',color='white',fontsize=9,fontweight='bold')
ax[1].set_xticks(range(6),lengths);ax[1].set_yticks(range(4),['4/5','4/6','4/7','4/8'])
ax[1].set_xlabel('Payload length (bytes)');ax[1].set_ylabel('Coding rate')
ax[1].set_title('B  Native SF7 randomized matrix',loc='left',pad=17)
k=sum(c['passed'] for c in matrix['cases']);n=len(matrix['cases']);lo,hi=wilson(k,n)
ax[1].text(.5,-.28,f'{k}/{n} = {100*k/n:.2f}% · 95% CI {100*lo:.2f}–{100*hi:.2f}%',transform=ax[1].transAxes,ha='center',fontsize=8)

for cr,color in zip(range(1,5),['#3e6b9a','#319c94','#b78b33','#804d91']):
    times=[]
    for length in lengths:
        case=next(c for c in matrix['cases'] if c['cr']==cr and c['length']==length and c['transportOk'])
        times.append(float(case['end'].split()[5]))
    ax[2].plot(lengths,times,'o-',markersize=3,color=color,label=f'CR4/{cr+4}')
ax[2].set_title('C  Calculated PHY airtime',loc='left',pad=17)
ax[2].set_xlabel('Payload length (bytes)');ax[2].set_ylabel('Frame duration (ms)')
ax[2].grid(alpha=.15);ax[2].legend(frameon=False,fontsize=8)
fig.subplots_adjust(left=.06,right=.99,top=.83,bottom=.28,wspace=.4)
fig.text(.06,.045,'2440.125 MHz · BW203.125 kHz · SF7 · explicit header + CRC · stationary indoor bench\n'
    'Error bars: Wilson 95% intervals. RF power was not calibrated. Panel C is encoder airtime, not measured on-air duration.',fontsize=8,color='#425465')
for suffix in ('svg','pdf','png'):fig.savefig(OUT/f'baseline-results.{suffix}',dpi=240,facecolor='white')
print(f'{k}/{n}: Wilson 95% [{100*lo:.4f}, {100*hi:.4f}]')
