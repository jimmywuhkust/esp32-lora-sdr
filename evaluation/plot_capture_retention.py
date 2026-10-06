"""Finite real capture windows; incomplete batches remain visibly incomplete."""
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
ROOT=Path(__file__).resolve().parent.parent
data=json.loads((ROOT/'evaluation/data/research/capture-retention-summary.json').read_text())
selected=[data[0],data[4],data[5]]
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'svg.fonttype':'none','pdf.fonttype':42,'axes.spines.top':False,'axes.spines.right':False})
fig,axes=plt.subplots(1,2,figsize=(11.8,5.2))
labels=['SRAM queue\n108 windows','PSRAM + verified trim\n106 windows, then abort','PSRAM + verified trim\n36-window new block']
for i,d in enumerate(selected):
    rate=100*d['fullRetentionWindows']/d['completedWindows']
    axes[0].bar(i,rate,color=['#7793a8','#237b85','#237b85'][i],width=.6)
    axes[0].text(i,rate+2,f"{d['fullRetentionWindows']}/{d['completedWindows']}",ha='center')
    for offset,sf in enumerate((7,8,9)):
        q=d['bySF'][str(sf)];x=i+(offset-1)*.24
        axes[1].bar(x,100*q['passed']/q['windows'],width=.22,color=['#7793a8','#237b85','#d9934b'][offset],label=f'SF{sf}' if i==0 else None)
        axes[1].text(x,100*q['passed']/q['windows']+1,f"{q['passed']}/{q['windows']}",ha='center',fontsize=8,rotation=90)
for ax in axes:
    ax.set_xticks(range(3),labels,fontsize=8);ax.set_ylim(0,119);ax.set_yticks([0,25,50,75,100]);ax.grid(axis='y',alpha=.16);ax.set_axisbelow(True)
axes[0].set_title('A  Completed windows retaining all output IQ',loc='left',fontweight='bold');axes[0].set_ylabel('Windows (%)')
axes[1].set_title('B  Full bytes + PC payload CRC',loc='left',fontweight='bold');axes[1].set_ylabel('Packets (%)');axes[1].legend(loc='lower right',frameon=False)
fig.subplots_adjust(left=.07,right=.97,top=.87,bottom=.30,wspace=.25)
fig.text(.07,.055,'Actual stationary board-pair measurements: BW203.125 kHz, 250 kcomplex samples/s, 4-bit components, 500-ms windows.\nSeparate fresh payload batches, four CRs, lengths 1/8/32; no RF retries. The 107th PSRAM attempt aborted on a ring edge.\nOnly completed windows form plotted denominators. Two earlier PSRAM batches also aborted; see all retained raw reports.\n100% reported output retention is not proof of every physical ADC sample, full-time coverage or continuous 4 MS/s USB.',fontsize=8,color='#425465')
for ext in ('svg','png','pdf'):fig.savefig(ROOT/f'docs/assets/capture-retention.{ext}',dpi=240,facecolor='white')
print('Saved measured finite-window retention figure')
