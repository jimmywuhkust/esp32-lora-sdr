"""Actual public LR2021 -> XIAO IQ -> PC measurements, never simulated RF."""
import hashlib,json,re,sys
from pathlib import Path
import numpy as np
from scipy.signal import spectrogram
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

ROOT=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(ROOT/'host'))
from lora_packet_iq import contiguous_runs,frames_from_file

data=ROOT/'evaluation/data/research'
before=json.loads((data/'live-reverse-first-frame-sf7-9.json').read_text())
after=json.loads((data/'live-reverse-public-irq-only-complete.json').read_text())
if not before['completed'] or not after['completed']:raise ValueError('Incomplete RF batch')
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.spines.top':False,
    'axes.spines.right':False,'svg.fonttype':'none','pdf.fonttype':42,'axes.titleweight':'bold'})
fig,axes=plt.subplots(2,2,figsize=(11.8,7.8),gridspec_kw={'height_ratios':[.85,1.1]})
heat=np.zeros((3,2)); labels=[]
for row,sf in enumerate((7,8,9)):
    for col,batch in enumerate((before,after)):
        cases=[c for c in batch['cases'] if c['sf']==sf]
        if len(cases)!=3:raise ValueError('Unexpected sample size')
        k=sum(c['passed'] for c in cases);heat[row,col]=k/3
        axes[0,0].text(col,row,f'{k}/3',ha='center',va='center',color='white' if k else '#203246',fontweight='bold')
    labels.append(f'SF{sf}')
axes[0,0].imshow(heat,vmin=0,vmax=1,cmap='YlGnBu',aspect='auto')
axes[0,0].set_yticks(range(3),labels)
axes[0,0].set_xticks(range(2),['GPIO completion','Chip IRQ completion'])
axes[0,0].set_title('A  Full bytes + payload CRC',loc='left',pad=12)
for col,sf in enumerate((7,8,9)):
    cases=[c for c in after['cases'] if c['sf']==sf]
    durations=[]
    for case in cases:
        diag=' '.join(case['txDiagnostics'])
        match=re.search(r'elapsed_us=(\d+)',diag)
        if not match:raise ValueError('Missing measured completion time')
        durations.append(int(match[1])/1000)
    axes[0,1].scatter([col]*len(durations),durations,color='#237b85',s=32)
    axes[0,1].annotate(f'{np.median(durations):.1f} ms',(col,max(durations)),
        xytext=(0,10),textcoords='offset points',ha='center')
axes[0,1].set_xticks(range(3),labels);axes[0,1].set_ylim(0,290);axes[0,1].set_xlim(-.4,2.4)
axes[0,1].set_ylabel('Command to completed TX (ms)');axes[0,1].grid(axis='y',alpha=.18)
axes[0,1].set_title('B  Actual chip completion, 32 bytes / CR4/8',loc='left',pad=12)
for ax,sf in zip(axes[1],(8,9)):
    manifest=json.loads((ROOT/f'host/samples/lr2021-sf{sf}-manifest.json').read_text())
    path=ROOT/'host/samples'/manifest['file']
    if hashlib.sha256(path.read_bytes()).hexdigest()!=manifest['sha256']:raise ValueError('Capture checksum changed')
    runs=list(contiguous_runs(frames_from_file(path)))
    if len(runs)!=1:raise ValueError('Figure requires an actual contiguous window')
    index,iq=runs[0]
    f,t,p=spectrogram(iq,fs=250000,nperseg=128,noverlap=112,return_onesided=False,
        detrend=False,mode='psd',scaling='density')
    f=np.fft.fftshift(f);p=np.fft.fftshift(p,axes=0)
    power=10*np.log10(np.maximum(p,1e-15)/p.max())
    image=ax.pcolormesh(t*1000,f/1000,power,vmin=-45,vmax=0,cmap='magma',shading='auto',rasterized=True)
    ax.set_xlim(0,350);ax.set_ylim(-125,125);ax.set_xlabel('Time within actual capture (ms)')
    ax.set_ylabel('Offset from receiver center (kHz)')
    ax.set_title(f'{"C" if sf==8 else "D"}  Real SF{sf} packet · CRC {manifest["expectedPayloadCRC"]}',loc='left',pad=12)
    bar=fig.colorbar(image,ax=ax,pad=.025,fraction=.035);bar.set_label('Relative PSD (dB)')
fig.subplots_adjust(left=.07,right=.94,top=.9,bottom=.17,hspace=.48,wspace=.38)
fig.text(.07,.035,'A–B: separate fresh 9-packet batches; same stationary board pair, BW203.125 kHz, 32-byte payload, CR4/8; no retries.\n'
    'C–D: raw XIAO IQ, 250 kcomplex samples/s, 4-bit components, hardware AGC; each panel normalized to its own maximum.\n'
    '350-ms contiguous windows are not continuous full-time USB capture. Complete packet decoding runs on the PC, not Arduino.',fontsize=8,color='#425465')
for ext in ('svg','png','pdf'):fig.savefig(ROOT/f'docs/assets/reverse-results.{ext}',dpi=240,facecolor='white')
plt.close(fig)
print('Saved actual reverse-RF figures with checksum-verified SF8/SF9 IQ.')
