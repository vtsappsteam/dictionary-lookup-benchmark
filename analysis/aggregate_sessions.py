#!/usr/bin/env python3
"""Combine three measurement sessions into the numbers reported in the paper.

Timing is measured in three separate sessions and the median session is
reported, with the spread across sessions recorded alongside it. Quantities
that must be deterministic, memory footprint and false positive counts, are
required to be identical in all three; the script fails if they are not.

Input:  results/s{1,2,3}/*.json
Output: results/final_numbers.json, plus the tables printed to stdout
"""
import json
import sys
from pathlib import Path

import numpy as np

BASE = Path(__file__).resolve().parents[1] / 'results'
SESSIONS = ['s1', 's2', 's3']
OUT = BASE / 'final_numbers.json'


def load(sess, fname):
    p = BASE / sess / fname
    if not p.exists():
        return None
    return json.loads(p.read_text(encoding='utf-8'))


def structs(doc):
    return {s['name']: s for s in doc['structures']}


def combine(fname):
    """Per structure: median timing across sessions, spread, deterministic fields."""
    docs = [load(s, fname) for s in SESSIONS]
    if any(d is None for d in docs):
        return None
    per = [structs(d) for d in docs]
    names = list(per[0])
    out = {}
    for n in names:
        med = [p[n]['lookup_time_per_query']['median'] * 1e6 for p in per]
        mem = {p[n]['memory_bytes'] for p in per}
        fpc = {p[n].get('fp_count') for p in per}
        if len(mem) != 1:
            sys.exit('FAIL: %s / %s memory differs across sessions: %s' % (fname, n, mem))
        if len(fpc) != 1:
            sys.exit('FAIL: %s / %s false positive count differs: %s' % (fname, n, fpc))
        out[n] = {
            'lookup_us_median_of_sessions': round(float(np.median(med)), 4),
            'lookup_us_per_session': [round(x, 4) for x in med],
            'lookup_us_range_pct': round(100.0 * (max(med) - min(med)) / np.median(med), 1),
            'memory_mb': per[0][n]['memory_mb'],
            'build_s_median_of_sessions': round(float(np.median(
                [p[n]['build_time']['median'] for p in per])), 4),
        }
        if per[0][n].get('fp_count') is not None:
            out[n]['fp_count'] = per[0][n]['fp_count']
            out[n]['fp_tested'] = per[0][n]['fp_tested']
            out[n]['fp_rate_percent'] = round(
                100.0 * per[0][n]['fp_count'] / per[0][n]['fp_tested'], 4)
    return out


def combine_scenarios():
    docs = [load(s, 'srlex_scenarios.json') for s in SESSIONS]
    if any(d is None for d in docs):
        return None
    out = {}
    for scen in ('hit_100', 'mix_50_50', 'miss_100'):
        per = [{s['name']: s for s in d['scenarios'][scen]} for d in docs]
        for n in per[0]:
            med = [p[n]['lookup_time_per_query']['median'] * 1e6 for p in per]
            out.setdefault(n, {})[scen] = round(float(np.median(med)), 4)
    for n in out:
        out[n]['miss_over_hit'] = round(out[n]['miss_100'] / out[n]['hit_100'], 3)
    return out


def main():
    res = {}
    for f in ('srlex_c.json', 'english_c.json', 'srlex_236k_c.json',
              'bloom_at_275.json', 'bloom_at_0012.json'):
        r = combine(f)
        if r:
            res[f[:-5]] = r
    sc = combine_scenarios()
    if sc:
        res['scenarios'] = sc

    print('=== Table I: full Serbian dictionary, C (median of three sessions) ===')
    print('%-28s %9s %8s %9s %10s %12s' %
          ('structure', 'lookup us', 'spread', 'MB', 'build s', 'FP %'))
    for n, v in res['srlex_c'].items():
        fp = ('%.4f' % v['fp_rate_percent']) if 'fp_rate_percent' in v else '-'
        print('%-28s %9.3f %7.1f%% %9.2f %10.3f %12s' %
              (n, v['lookup_us_median_of_sessions'], v['lookup_us_range_pct'],
               v['memory_mb'], v['build_s_median_of_sessions'], fp))

    if 'scenarios' in res:
        print('\n=== Query scenarios, C ===')
        print('%-28s %9s %9s %9s %9s' % ('structure', 'hit', '50/50', 'miss', 'miss/hit'))
        for n, v in res['scenarios'].items():
            print('%-28s %9.3f %9.3f %9.3f %9.2f' %
                  (n, v['hit_100'], v['mix_50_50'], v['miss_100'], v['miss_over_hit']))

    if 'srlex_236k_c' in res and 'english_c' in res:
        print('\n=== Matched size: Serbian 236K against English 236K ===')
        print('%-28s %9s %9s %9s %9s' % ('structure', 'SR us', 'EN us', 'SR MB', 'EN MB'))
        for n in res['srlex_236k_c']:
            if n not in res['english_c']:
                continue
            a, b = res['srlex_236k_c'][n], res['english_c'][n]
            print('%-28s %9.3f %9.3f %9.2f %9.2f' %
                  (n, a['lookup_us_median_of_sessions'], b['lookup_us_median_of_sessions'],
                   a['memory_mb'], b['memory_mb']))

    print('\n=== Filters at a matched false positive rate ===')
    pairs = [('Bloom Filter', 'bloom_at_275', 'Cuckoo Filter', 'srlex_c'),
             ('Bloom Filter', 'bloom_at_0012', 'Cuckoo Filter 16-bit', 'srlex_c')]
    for bn, bk, cn, ck in pairs:
        if bk not in res:
            continue
        b, c = res[bk][bn], res[ck][cn]
        print('  %-22s FP %.4f %%  %5.2f MB  %6.3f us' %
              ('Bloom', b['fp_rate_percent'], b['memory_mb'],
               b['lookup_us_median_of_sessions']))
        print('  %-22s FP %.4f %%  %5.2f MB  %6.3f us' %
              (cn, c['fp_rate_percent'], c['memory_mb'],
               c['lookup_us_median_of_sessions']))
        print()

    OUT.write_text(json.dumps(res, indent=2, ensure_ascii=False), encoding='utf-8')
    print('Written: %s' % OUT)
    return 0


if __name__ == '__main__':
    sys.exit(main())
