"""One fresh RF attempt per native TX setting; no reliability extrapolation."""
from plot_native import ROOT, load, save, plt, np
from matplotlib.colors import ListedColormap

cases = load('native-tx-rearmed.json')['cases']
lengths = [1, 8, 32, 80, 255]
matrix = np.zeros((4, 5))
for cr in range(1, 5):
    for j, length in enumerate(lengths):
        rows = [c for c in cases if c['codingRate'] == cr and c['bytes'] == length]
        if len(rows) != 1: raise ValueError('This figure requires one attempt per cell')
        matrix[cr-1, j] = int(rows[0]['passed'])
fig, ax = plt.subplots(figsize=(8.1, 4.4))
ax.imshow(matrix, cmap=ListedColormap(['#f2d4cd', '#d1e9e8']), vmin=0, vmax=1, aspect='auto')
for i in range(4):
    for j in range(5):
        ax.text(j, i, '1/1' if matrix[i, j] else '0/1*', ha='center', va='center', color='#263949')
ax.set_xticks(range(5), lengths)
ax.set_yticks(range(4), ['4/5', '4/6', '4/7', '4/8'])
ax.set_xlabel('Payload length (bytes)')
ax.set_ylabel('Coding rate')
ax.set_title('ESP32 native TX → LR2021: strict packet acceptance', loc='left', pad=14)
ax.set_xticks(np.arange(-.5, 5, 1), minor=True)
ax.set_yticks(np.arange(-.5, 4, 1), minor=True)
ax.grid(which='minor', color='white', linewidth=2)
ax.tick_params(which='minor', length=0)
fig.text(.11, .08, 'SF7 · BW203.125 kHz · 19/20 accepted · all 20 payloads exact\n'
         '*Mixed header-valid/header-error IRQ: rejected. One fresh attempt per cell; one indoor board pair.', fontsize=8)
fig.subplots_adjust(left=.11, right=.97, top=.84, bottom=.25)
save(fig, 'native-transmission')
svg = ROOT / 'docs/assets/native-transmission.svg'
svg.write_text('\n'.join(line.rstrip() for line in svg.read_text(encoding='utf-8').splitlines())+'\n', encoding='utf-8')
