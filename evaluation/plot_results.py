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

power=read('native-amplitude-sf.json')
amplitudes=[1,3,10,30,75,150]
fig,axes=plt.subplots(1,2,figsize=(10.4,4.1))
for sf,color in zip([7,8,9],['#237b85','#9b583b','#625d91']):
    percentages=[];lower=[];upper=[]
    for amplitude in amplitudes:
        cases=[c for c in power['cases'] if c['sf']==sf and c['amplitude']==amplitude]
        k=sum(c['passed'] for c in cases);n=len(cases);lo,hi=wilson(k,n)
        percentages.append(100*k/n);lower.append(max(0,100*(k/n-lo)));upper.append(max(0,100*(hi-k/n)))
    axes[0].errorbar(amplitudes,percentages,yerr=[lower,upper],label=f'SF{sf}',
        marker='o',markersize=4,capsize=3,color=color,linestyle='-' if sf==7 else '--' if sf==8 else ':')
axes[0].set_xscale('log');axes[0].set_ylim(-4,110);axes[0].set_xticks(amplitudes,amplitudes)
axes[0].set_ylabel('Exact-payload, CRC-valid reception (%)')
axes[0].set_title('A  Amplitude sweep, 10 trials per point',loc='left',pad=14)
axes[0].legend(frameon=False);axes[0].grid(alpha=.15)
x=[];rssis=[];counts=[]
for amplitude in amplitudes:
    values=[c['matched']['rssi'] for c in power['cases'] if c['sf']==7 and c['amplitude']==amplitude and c['matched']]
    if values:x.append(amplitude);rssis.append(np.median(values));counts.append(len(values))
axes[1].plot(x,rssis,'o-',color='#237b85')
for value,rssi,count in zip(x,rssis,counts):axes[1].annotate(f'n={count}',(value,rssi),xytext=(0,10),textcoords='offset points',ha='center',fontsize=8)
axes[1].set_xscale('log');axes[1].set_xticks(amplitudes,amplitudes);axes[1].set_ylim(-91,-56)
axes[1].set_ylabel('LR2021 reported RSSI (dBm)')
axes[1].set_title('B  SF7 successful packets only',loc='left',pad=14);axes[1].grid(alpha=.15)
for axis in axes:axis.set_xlabel('DAC amplitude (uncalibrated code)')
fig.subplots_adjust(left=.085,right=.985,top=.82,bottom=.28,wspace=.38)
fig.text(.085,.055,'2440.125 MHz · BW203.125 kHz · CR4/8 · 32 random bytes · stationary indoor bench\n'
    '180 trials, randomized blocks. Wilson 95% intervals. RSSI excludes failed packets; amplitude is not RF power.',fontsize=8,color='#425465')
for suffix in ('svg','pdf','png'):fig.savefig(OUT/f'amplitude-results.{suffix}',dpi=240,facecolor='white')

public=read('public-receiver.json')
if public['completed']:
    fig,axes=plt.subplots(1,2,figsize=(10.5,4.0))
    for axis,dataset,lengths,title in zip(axes,[matrix,public],[[1,8,32,80,128,250],[1,8,32,80,128,255]],
            ['A  Original HF firmware','B  Public RadioLib receiver']):
        heat=np.empty((4,6))
        for i in range(4):
            for j,length in enumerate(lengths):
                rows=[c for c in dataset['cases'] if c['cr']==i+1 and c['length']==length]
                heat[i,j]=sum(c['passed'] for c in rows)/len(rows)
        axis.imshow(heat,vmin=.5,vmax=1,cmap='YlGnBu',aspect='auto')
        for i in range(4):
            for j,length in enumerate(lengths):
                rows=[c for c in dataset['cases'] if c['cr']==i+1 and c['length']==length]
                axis.text(j,i,f"{sum(c['passed'] for c in rows)}/{len(rows)}",ha='center',va='center',fontsize=9,color='white',fontweight='bold')
        axis.set_xticks(range(6),lengths);axis.set_yticks(range(4),['4/5','4/6','4/7','4/8'])
        axis.set_ylabel('Coding rate');axis.set_xlabel('Payload length (bytes)');axis.set_title(title,loc='left',pad=15)
        k=sum(c['passed'] for c in dataset['cases']);n=len(dataset['cases']);lo,hi=wilson(k,n)
        axis.text(.5,-.28,f'{k}/{n} · {100*k/n:.2f}% · 95% CI {100*lo:.2f}–{100*hi:.2f}%',transform=axis.transAxes,ha='center',fontsize=8)
    fig.subplots_adjust(left=.07,right=.985,top=.82,bottom=.33,wspace=.36)
    fig.text(.07,.035,'Exact bytes + independent payload CRC · SF7 · 2440.125 MHz · BW203.125 kHz\n'
        'Different payload sets and firmware builds; two separate datasets, not a controlled receiver comparison.',fontsize=8,color='#425465')
    for suffix in ('svg','pdf','png'):fig.savefig(OUT/f'public-receiver-results.{suffix}',dpi=240,facecolor='white')

bwsets=[public,read('bandwidth-sf7-406-matrix.json'),read('bandwidth-sf7-812-matrix.json')]
fig,axes=plt.subplots(1,2,figsize=(10.4,4.0))
for i,dataset in enumerate(bwsets):
    k=sum(c['passed'] for c in dataset['cases']);n=len(dataset['cases']);lo,hi=wilson(k,n)
    axes[0].errorbar(i,100*k/n,yerr=[[100*(k/n-lo)],[100*(hi-k/n)]],fmt='o',capsize=5,color='#237b85')
    axes[0].annotate(f'{k}/{n}',(i,100*k/n),xytext=(0,-25),textcoords='offset points',ha='center')
axes[0].set_xticks(range(3),['203.125','406.25','812.5']);axes[0].set_ylim(91,101.5);axes[0].set_xlim(-.5,2.5)
axes[0].set_xlabel('Bandwidth (kHz)');axes[0].set_ylabel('Exact bytes + CRC reception (%)')
axes[0].set_title('A  Separate SF7 bench matrices',loc='left',pad=14);axes[0].grid(axis='y',alpha=.15)
lengths=[1,8,32,80,128,255]
for dataset,color in zip(bwsets,['#237b85','#9b583b','#625d91']):
    times=[]
    for length in lengths:
        row=next(c for c in dataset['cases'] if c['cr']==4 and c['length']==length)
        times.append(float(row['end'].split()[5]))
    axes[1].plot(lengths,times,'o-',markersize=3,color=color,label=f'{dataset["profile"]["bandwidthHz"]/1000:g} kHz')
axes[1].set_title('B  Calculated SF7/CR4/8 airtime',loc='left',pad=14)
axes[1].set_xlabel('Payload length (bytes)');axes[1].set_ylabel('Frame duration (ms)')
axes[1].grid(alpha=.15);axes[1].legend(frameon=False,fontsize=8)
fig.subplots_adjust(left=.09,right=.98,top=.82,bottom=.29,wspace=.4)
fig.text(.09,.045,'LR2021 independent receiver · 2440.125 MHz · explicit header + CRC · four coding rates · 1–255 bytes\n'
    'Separate builds / shorter wider-band runs, not a controlled sensitivity comparison. Wilson 95% intervals; airtime is theoretical.',fontsize=8,color='#425465')
for suffix in ('svg','pdf','png'):fig.savefig(OUT/f'bandwidth-results.{suffix}',dpi=240,facecolor='white')

# Matplotlib's multiline SVG paths contain trailing blanks by default.
for path in OUT.glob('*-results.svg'):
    path.write_text('\n'.join(line.rstrip() for line in path.read_text(encoding='utf-8').splitlines())+'\n',encoding='utf-8')
