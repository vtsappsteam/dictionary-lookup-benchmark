#!/usr/bin/env python3
"""Paired significance tests for the lookup-time comparison.

Every structure is measured on the same query set: benchmark.py draws the
query words once per dataset with a fixed seed and passes the same list to
each structure, and benchmark.c does the same. The observations are
therefore paired, so this script reports the Wilcoxon signed-rank test
rather than the rank-sum test.

Computed from the per-run arrays stored in the result files:
  - median and interquartile range per structure,
  - signed-rank test for all 21 pairs, paired by run index,
  - Holm-Bonferroni correction across those 21 comparisons,
  - effect size, as Cliff's delta and as a ratio of medians,
  - a check on whether pairing by run index carries any information:
    the Spearman correlation between structures across runs. Near zero
    means the runs share no common factor, which the paper states.

Usage:  python3 analysis/recompute_stats.py [session]      (default s1)
Input:  results/<session>/{srlex_c,srlex_py}.json
Output: results/stats_paired_<session>.json
"""
import itertools
import json
import sys
from pathlib import Path

import numpy as np
from scipy import stats

BASE = Path(__file__).resolve().parents[1]
SESSION = sys.argv[1] if len(sys.argv) > 1 else 's1'
RES = BASE / 'results' / SESSION
OUT = BASE / 'results' / ('stats_paired_%s.json' % SESSION)


def cliffs_delta(a, b):
    """Cliff's delta: P(a>b) - P(a<b). Positive means `a` is the slower one."""
    a, b = np.asarray(a), np.asarray(b)
    gt = sum((x > b).sum() for x in a)
    lt = sum((x < b).sum() for x in a)
    return (gt - lt) / (len(a) * len(b))


def holm(pvals):
    """Holm-Bonferroni, returning adjusted p-values in the input order."""
    m = len(pvals)
    order = np.argsort(pvals)
    adj = np.empty(m)
    running = 0.0
    for rank, idx in enumerate(order):
        val = (m - rank) * pvals[idx]
        running = max(running, val)
        adj[idx] = min(1.0, running)
    return adj


def analyse(tag):
    d = json.loads((RES / f'{tag}.json').read_text(encoding='utf-8'))
    names, series = [], {}
    for s in d['structures']:
        names.append(s['name'])
        series[s['name']] = np.array(s['lookup_time_per_query']['all']) * 1e6  # us

    out = {'dataset': tag, 'num_runs': d['num_runs'], 'num_lookups': d['num_lookups'],
           'per_structure': {}, 'pairs': []}

    print('\n=== %s: median and IQR of lookup time (us) ===' % tag)
    for n in names:
        v = series[n]
        q1, q3 = np.percentile(v, [25, 75])
        out['per_structure'][n] = {
            'median_us': round(float(np.median(v)), 4),
            'iqr_us': round(float(q3 - q1), 4),
            'q1_us': round(float(q1), 4), 'q3_us': round(float(q3), 4),
            'n_runs': int(len(v)),
        }
        print('  %-28s %7.3f  IQR %6.3f' % (n, np.median(v), q3 - q1))

    # does pairing by run index carry anything
    rhos = []
    for a, b in itertools.combinations(names, 2):
        rho = stats.spearmanr(series[a], series[b]).statistic
        rhos.append(rho)
    out['pairing_check'] = {
        'spearman_median': round(float(np.median(rhos)), 3),
        'spearman_min': round(float(np.min(rhos)), 3),
        'spearman_max': round(float(np.max(rhos)), 3),
        'share_significant_positive': round(
            float(np.mean([r > 0.36 for r in rhos])), 3),  # p<0.05 at n=30
    }
    print('  pairing check: Spearman across runs, median %.3f (from %.3f to %.3f)'
          % (out['pairing_check']['spearman_median'],
             out['pairing_check']['spearman_min'],
             out['pairing_check']['spearman_max']))

    pairs, praw = [], []
    for a, b in itertools.combinations(names, 2):
        x, y = series[a], series[b]
        w = stats.wilcoxon(x, y)
        r = stats.ranksums(x, y)
        pairs.append({
            'a': a, 'b': b,
            'median_a_us': round(float(np.median(x)), 4),
            'median_b_us': round(float(np.median(y)), 4),
            'ratio_median': round(float(np.median(x) / np.median(y)), 3),
            'signed_rank_stat': float(w.statistic),
            'signed_rank_p': float(w.pvalue),
            'rank_sum_p': float(r.pvalue),
            'cliffs_delta': round(float(cliffs_delta(x, y)), 3),
        })
        praw.append(w.pvalue)

    adj = holm(np.array(praw))
    n_sig = 0
    for p, a_ in zip(pairs, adj):
        p['signed_rank_p_holm'] = float(a_)
        p['significant_holm_005'] = bool(a_ < 0.05)
        n_sig += p['significant_holm_005']
    out['pairs'] = pairs
    out['n_pairs'] = len(pairs)
    out['n_significant_holm'] = n_sig

    print('  %d pairs, %d significant after Holm correction' % (len(pairs), n_sig))
    ns = [p for p in pairs if not p['significant_holm_005']]
    for p in ns:
        print('    NOT significant: %s vs %s (p_holm %.3f, median ratio %.2f)'
              % (p['a'], p['b'], p['signed_rank_p_holm'], p['ratio_median']))
    return out


def main():
    res = {t: analyse(t) for t in ('srlex_c', 'srlex_py')}
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding='utf-8')
    print('\nWritten: %s' % OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main())
