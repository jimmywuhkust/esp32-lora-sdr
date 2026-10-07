"""Plot fixed, separate hardware datasets; never substitute retries for misses."""
import json,pathlib
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

ROOT=pathlib.Path(__file__).resolve().parents[1]
def read(name):return json.loads((ROOT/'evaluation/data'/name).read_text())
def main():
    before=read('arduino-three-radios-sixteen-bit.json')
    after=read('arduino-three-radios-final.json')
    sessions=[read('arduino-ping-pong-final-ab.json'),read('arduino-ping-pong-final-ba.json')]
    pairs=[('A','LR2021'),('B','LR2021'),('LR2021','A'),('LR2021','B'),('A','B'),('B','A')]
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,
                         'axes.spines.right':False,'svg.fonttype':'none'})
    fig,(ax,bx)=plt.subplots(1,2,figsize=(12.4,4.4),gridspec_kw={'width_ratios':[1.6,1]})
    x=np.arange(len(pairs));width=.34
    for delta,report,color,label in [(-width/2,before,'#bbc7d3','Earlier: hard FEC'),(width/2,after,'#008b87','Final: soft FEC / bounded TX')]:
        counts=[];totals=[]
        for source,target in pairs:
            cases=[c for c in report['cases'] if (c['source'],c['target'])==(source,target)]
            counts.append(sum(c['passed'] for c in cases));totals.append(len(cases))
        bars=ax.bar(x+delta,np.array(counts)/np.array(totals),width,color=color,label=label)
        for bar,k,n in zip(bars,counts,totals):ax.text(bar.get_x()+bar.get_width()/2,bar.get_height()+.025,f'{k}/{n}',ha='center',fontsize=9)
    ax.set_xticks(x,[f'{a}\n→ {b}' for a,b in pairs]);ax.set_ylim(0,1.17)
    ax.set_yticks([0,.5,1],['0%','50%','100%']);ax.set_ylabel('Full payload + valid CRC')
    ax.set_title('A · Six directed RF links',loc='left',weight='bold',pad=18)
    ax.legend(loc='upper center',bbox_to_anchor=(.5,-.16),frameon=False,ncol=2,fontsize=9)
    ax.grid(axis='y',alpha=.15);ax.set_axisbelow(True)
    for index,session in enumerate(sessions):
        results=session['results'];passed=sum(r['crcExactPong'] for r in results)
        bx.bar(index,passed/len(results),color='#008b87',width=.52)
        bx.text(index,passed/len(results)+.03,f'{passed}/{len(results)} unique requests',ha='center',fontsize=10)
    bx.set_xticks([0,1],['A initiates → B','B initiates → A']);bx.set_ylim(0,1.17)
    bx.set_yticks([0,.5,1],['0%','50%','100%']);bx.grid(axis='y',alpha=.15);bx.set_axisbelow(True)
    bx.set_title('B · Autonomous ping / pong',loc='left',weight='bold',pad=18)
    bx.text(.5,-.23,'Both MCUs encode, decode and check CRC.\nRF copies are explicitly counted in the report.',transform=bx.transAxes,ha='center',fontsize=9,color='#425166')
    fig.suptitle('ESP32-S3 LoRa · Arduino on-device TX / RX',x=.06,ha='left',weight='bold',fontsize=16)
    fig.text(.06,.04,'2440.125 MHz · BW 203.125 kHz · SF7 · stationary indoor bench · small, separate datasets',fontsize=9,color='#425166')
    fig.subplots_adjust(left=.06,right=.98,top=.77,bottom=.28,wspace=.28)
    target=ROOT/'docs/assets';target.mkdir(exist_ok=True)
    for suffix in ('svg','png','pdf'):fig.savefig(target/f'arduino-duplex.{suffix}',dpi=180,facecolor='white')
    plt.close(fig)
if __name__=='__main__':main()
