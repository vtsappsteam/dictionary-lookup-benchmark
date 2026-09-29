#!/usr/bin/env python3
"""
Figures for the ICAI 2026 paper.

Tables carry the exact numbers, figures carry the trends.
  - Fig 1: lookup time against dictionary size (C)
  - Fig 2: memory against dictionary size (C)
  - Fig 3: Python to C speedup per structure
  - Fig 4: Serbian against English, at matched dictionary size (C)

Authors: Nikola Vukotic, Slavimir Stosovic
"""

import json
import os
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

RESULTS_DIR = os.path.join(os.path.dirname(__file__), '..', 'results')
FIGURES_DIR = os.path.join(RESULTS_DIR, 'figures')
os.makedirs(FIGURES_DIR, exist_ok=True)

# Plot style
plt.rcParams.update({
    'font.size': 9,
    'font.family': 'serif',
    'figure.dpi': 300,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
    'axes.grid': True,
    'grid.alpha': 0.3,
    'grid.linestyle': '--',
})

STRUCT_ORDER = [
    'HashMap',
    'Sorted Array + Binary Search',
    'Trie',
    'Ternary Search Tree',
    'DAWG',
    'Bloom Filter',
    'Cuckoo Filter',
]

SHORT_NAMES = {
    'HashMap': 'HashMap',
    'Sorted Array + Binary Search': 'Sorted\nArray',
    'Trie': 'Trie',
    'Ternary Search Tree': 'TST',
    'DAWG': 'DAWG',
    'Bloom Filter': 'Bloom\nFilter',
    'Cuckoo Filter': 'Cuckoo\nFilter',
}

COLORS = {
    'HashMap': '#2196F3',
    'Sorted Array + Binary Search': '#4CAF50',
    'Trie': '#FF9800',
    'Ternary Search Tree': '#9C27B0',
    'DAWG': '#F44336',
    'Bloom Filter': '#607D8B',
    'Cuckoo Filter': '#795548',
}

MARKERS = {
    'HashMap': 'o',
    'Sorted Array + Binary Search': 's',
    'Trie': '^',
    'Ternary Search Tree': 'D',
    'DAWG': 'v',
    'Bloom Filter': 'x',
    'Cuckoo Filter': '+',
}


def load_json(filename):
    with open(os.path.join(RESULTS_DIR, filename)) as f:
        return json.load(f)


def get_struct_data(data, name):
    for s in data['structures']:
        if s.get('name') == name:
            return s
    return None


# ============================================================
# Fig 1: Scalability - Lookup Time (C, Serbian)
# ============================================================

def fig1_scalability_lookup():
    data = load_json(os.path.join('s1', 'srlex_scalability.json'))

    fig, ax = plt.subplots(figsize=(5, 3.5))

    for struct_name in STRUCT_ORDER:
        sizes = []
        times = []
        for size_data in data['scalability']['sizes']:
            s = get_struct_data(size_data, struct_name)
            if s and 'lookup_time_per_query' in s:
                sizes.append(size_data['size'])
                times.append(s['lookup_time_per_query']['median'] * 1e6)

        if sizes:
            ax.plot(sizes, times,
                    marker=MARKERS[struct_name],
                    color=COLORS[struct_name],
                    label=SHORT_NAMES[struct_name].replace('\n', ' '),
                    linewidth=1.2,
                    markersize=4)

    ax.set_xlabel('Dictionary Size (number of words)')
    ax.set_ylabel('Lookup Time per Query (us)')
    ax.set_xscale('log')
    ax.legend(fontsize=7, loc='upper left', ncol=2)
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(
        lambda x, p: f'{x/1000:.0f}K' if x < 1e6 else f'{x/1e6:.1f}M'))

    plt.savefig(os.path.join(FIGURES_DIR, 'fig1_scalability_lookup.pdf'))
    plt.savefig(os.path.join(FIGURES_DIR, 'fig1_scalability_lookup.png'))
    plt.close()
    print('Fig 1: Scalability lookup time (C, Serbian) - OK')


# ============================================================
# Fig 2: Scalability - Memory (C, Serbian)
# ============================================================

def fig2_scalability_memory():
    data = load_json(os.path.join('s1', 'srlex_scalability.json'))

    fig, ax = plt.subplots(figsize=(5, 3.5))

    for struct_name in STRUCT_ORDER:
        sizes = []
        mems = []
        for size_data in data['scalability']['sizes']:
            s = get_struct_data(size_data, struct_name)
            if s and 'memory_mb' in s:
                sizes.append(size_data['size'])
                mems.append(s['memory_mb'])

        if sizes:
            ax.plot(sizes, mems,
                    marker=MARKERS[struct_name],
                    color=COLORS[struct_name],
                    label=SHORT_NAMES[struct_name].replace('\n', ' '),
                    linewidth=1.2,
                    markersize=4)

    ax.set_xlabel('Dictionary Size (number of words)')
    ax.set_ylabel('Memory Usage (MB)')
    ax.set_xscale('log')
    ax.set_yscale('log')
    ax.legend(fontsize=7, loc='upper left', ncol=2)
    ax.xaxis.set_major_formatter(ticker.FuncFormatter(
        lambda x, p: f'{x/1000:.0f}K' if x < 1e6 else f'{x/1e6:.1f}M'))

    plt.savefig(os.path.join(FIGURES_DIR, 'fig2_scalability_memory.pdf'))
    plt.savefig(os.path.join(FIGURES_DIR, 'fig2_scalability_memory.png'))
    plt.close()
    print('Fig 2: Scalability memory (C, Serbian) - OK')


# ============================================================
# Fig 3: Python vs C Speedup (srLex)
# ============================================================

def median_of_sessions_py(struct_name):
    vals = []
    for sess in ('s1', 's2', 's3'):
        data = load_json(os.path.join(sess, 'srlex_py.json'))
        s = get_struct_data(data, struct_name)
        vals.append(s['lookup_time_per_query']['median'] * 1e6)
    return sorted(vals)[1]


def fig3_speedup():
    fn = load_json('final_numbers.json')

    names = []
    speedups = []
    colors = []

    for struct_name in STRUCT_ORDER:
        c_us = fn['srlex_c'][struct_name]['lookup_us_median_of_sessions']
        py_us = median_of_sessions_py(struct_name)
        if c_us > 0:
            names.append(SHORT_NAMES[struct_name])
            speedups.append(py_us / c_us)
            colors.append(COLORS[struct_name])

    fig, ax = plt.subplots(figsize=(5, 3))

    bars = ax.bar(range(len(names)), speedups, color=colors, edgecolor='black', linewidth=0.5)
    ax.set_xticks(range(len(names)))
    ax.set_xticklabels(names, fontsize=7)
    ax.set_ylabel('Speedup (Python / C)')
    ax.axhline(y=1, color='black', linestyle='-', linewidth=0.5)

    # Value labels above the bars
    for bar, val in zip(bars, speedups):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.5,
                f'{val:.0f}x' if val >= 10 else f'{val:.1f}x',
                ha='center', va='bottom', fontsize=7)

    plt.savefig(os.path.join(FIGURES_DIR, 'fig3_speedup.pdf'))
    plt.savefig(os.path.join(FIGURES_DIR, 'fig3_speedup.png'))
    plt.close()
    print('Fig 3: Python vs C speedup - OK')
    for n, v in zip(names, speedups):
        print(f"    {n.replace(chr(10), ' '):14} {v:.1f}x")


# ============================================================
# Fig 4: Serbian vs English (C) - Grouped Bar
# ============================================================

def fig4_cross_language():
    fn = load_json('final_numbers.json')
    sr = fn['srlex_236k_c']
    en = fn['english_c']

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(6.5, 3))

    x = np.arange(len(STRUCT_ORDER))
    width = 0.35

    # Okabe-Ito pair: distinguishable for all types of color vision, and the
    # luminance ratio survives grayscale print; hatching is a second carrier.
    SR_COLOR = '#0072B2'
    EN_COLOR = '#E69F00'

    # Lookup time
    sr_times = [sr[name]['lookup_us_median_of_sessions'] for name in STRUCT_ORDER]
    en_times = [en[name]['lookup_us_median_of_sessions'] for name in STRUCT_ORDER]

    ax1.bar(x - width/2, sr_times, width, label='Serbian (236K sample)', color=SR_COLOR, edgecolor='black', linewidth=0.3)
    ax1.bar(x + width/2, en_times, width, label='English (236K)', color=EN_COLOR, edgecolor='black', linewidth=0.3, hatch='//')
    ax1.set_ylabel('Lookup Time (us)')
    ax1.set_xticks(x)
    ax1.set_xticklabels([SHORT_NAMES[n] for n in STRUCT_ORDER], fontsize=6)
    ax1.legend(fontsize=7)
    ax1.set_title('(a) Lookup Time', fontsize=9)

    # Memory
    sr_mems = [sr[name]['memory_mb'] for name in STRUCT_ORDER]
    en_mems = [en[name]['memory_mb'] for name in STRUCT_ORDER]

    ax2.bar(x - width/2, sr_mems, width, label='Serbian (236K sample)', color=SR_COLOR, edgecolor='black', linewidth=0.3)
    ax2.bar(x + width/2, en_mems, width, label='English (236K)', color=EN_COLOR, edgecolor='black', linewidth=0.3, hatch='//')
    ax2.set_ylabel('Memory (MB)')
    ax2.set_xticks(x)
    ax2.set_xticklabels([SHORT_NAMES[n] for n in STRUCT_ORDER], fontsize=6)
    ax2.set_ylim(0, 30)
    ax2.legend(fontsize=7)
    ax2.set_title('(b) Memory Usage', fontsize=9)

    plt.tight_layout()
    plt.savefig(os.path.join(FIGURES_DIR, 'fig4_cross_language.pdf'))
    plt.savefig(os.path.join(FIGURES_DIR, 'fig4_cross_language.png'))
    plt.close()
    print('Fig 4: Serbian vs English at matched size - OK')


# ============================================================
# MAIN
# ============================================================

if __name__ == '__main__':
    print('Generating figures for the ICAI 2026 paper...')
    print(f'Output: {FIGURES_DIR}')
    print()

    fig1_scalability_lookup()
    fig2_scalability_memory()
    fig3_speedup()
    fig4_cross_language()

    print(f'\nAll figures written to {FIGURES_DIR}/')
    for f in sorted(os.listdir(FIGURES_DIR)):
        size = os.path.getsize(os.path.join(FIGURES_DIR, f)) / 1024
        print(f'  {f} ({size:.0f} KB)')
