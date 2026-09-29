#!/usr/bin/env python3
"""
Lookup time under three query mixes.

Scenarios:
  - 100 percent hit, only words present in the dictionary
  - 50/50 mix
  - 100 percent miss, only out-of-vocabulary words

Out-of-vocabulary words are produced by altering a real word with one of
four edits, replace, insert, delete or swap a character, and are kept only
if the result is not itself in the dictionary. This measures how much of
the lookup cost is paid before a structure can reject a word.

Authors: Nikola Vukotic, Slavimir Stosovic
"""

import json
import os
import random
import string
import sys
import time
import statistics
from structures import ALL_STRUCTURES

RESULTS_DIR = os.path.join(os.path.dirname(__file__), '..', 'results')
NUM_RUNS = 30
NUM_LOOKUPS = 10_000
RANDOM_SEED = 42


def generate_oov_words(existing_set, count, rng, words):
    """
    Build out-of-vocabulary words by mutating real ones.
    Closer to real input than random strings, since they look like words.
    """
    oov = []
    word_list = list(words)
    chars = 'abcčćdđefghijklmnopqrsštuvwxyzž'

    while len(oov) < count:
        base = rng.choice(word_list)
        mutation = rng.randint(0, 3)

        if mutation == 0 and len(base) > 2:
            # Replace one character
            pos = rng.randint(0, len(base) - 1)
            new_char = rng.choice(chars)
            word = base[:pos] + new_char + base[pos+1:]
        elif mutation == 1:
            # Insert one character
            pos = rng.randint(0, len(base))
            word = base[:pos] + rng.choice(chars) + base[pos:]
        elif mutation == 2 and len(base) > 3:
            # Delete one character
            pos = rng.randint(0, len(base) - 1)
            word = base[:pos] + base[pos+1:]
        else:
            # Swap two characters
            if len(base) > 2:
                pos = rng.randint(0, len(base) - 2)
                word = base[:pos] + base[pos+1] + base[pos] + base[pos+2:]
            else:
                word = base + rng.choice(chars)

        if word not in existing_set and word not in oov:
            oov.append(word)

    return oov


def benchmark_scenario(StructureClass, words, lookup_words, scenario_name):
    """Benchmark one query scenario for one structure.
    Gradi strukturu iznova svaki run (isto kao glavni benchmark)
    da bi merenja bila konzistentna."""
    times = []
    for _ in range(NUM_RUNS):
        # Rebuild svaki put -- eliminise cache warming bias
        if StructureClass.probabilistic:
            struct = StructureClass(expected_items=len(words))
        else:
            struct = StructureClass()
        struct.build(words)

        t0 = time.perf_counter()
        for word in lookup_words:
            struct.lookup(word)
        elapsed = (time.perf_counter() - t0) / len(lookup_words)
        times.append(elapsed)

    return {
        'median_us': statistics.median(times) * 1e6,
        'mean_us': statistics.mean(times) * 1e6,
        'stdev_us': statistics.stdev(times) * 1e6 if len(times) > 1 else 0,
    }


def main():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument('--dataset', required=True)
    parser.add_argument('--name', default='dataset')
    parser.add_argument('--output', default=None)
    args = parser.parse_args()

    # Load words
    with open(args.dataset) as f:
        words = [line.strip() for line in f if line.strip()]
    word_set = set(words)

    print(f"Dataset: {args.name} ({len(words):,} words)")

    rng = random.Random(RANDOM_SEED)

    # Prepare query sets
    existing = rng.sample(words, min(NUM_LOOKUPS, len(words)))
    oov = generate_oov_words(word_set, NUM_LOOKUPS, rng, words)
    mixed = existing[:NUM_LOOKUPS//2] + oov[:NUM_LOOKUPS//2]
    rng.shuffle(mixed)

    scenarios = [
        ('100% Hit', existing),
        ('50/50 Mix', mixed),
        ('100% Miss', oov[:NUM_LOOKUPS]),
    ]

    results = {
        'dataset': args.name,
        'num_words': len(words),
        'num_runs': NUM_RUNS,
        'num_lookups': NUM_LOOKUPS,
        'structures': []
    }

    print(f"\n{'Structure':<30} {'100% Hit':>10} {'50/50 Mix':>10} {'100% Miss':>10}")
    print("-" * 62)

    for StructureClass in ALL_STRUCTURES:
        struct_result = {'name': StructureClass.name, 'scenarios': {}}

        hit_r = benchmark_scenario(StructureClass, words, scenarios[0][1], '100% Hit')
        mix_r = benchmark_scenario(StructureClass, words, scenarios[1][1], '50/50 Mix')
        miss_r = benchmark_scenario(StructureClass, words, scenarios[2][1], '100% Miss')

        struct_result['scenarios'] = {
            '100_hit': hit_r,
            '50_50_mix': mix_r,
            '100_miss': miss_r,
        }

        # Speedup ratio: miss vs hit
        if hit_r['median_us'] > 0:
            struct_result['miss_vs_hit_ratio'] = round(miss_r['median_us'] / hit_r['median_us'], 2)

        results['structures'].append(struct_result)

        print(f"{StructureClass.name:<30} {hit_r['median_us']:>9.2f} {mix_r['median_us']:>9.2f} {miss_r['median_us']:>9.2f}")

    # Save
    output = args.output or os.path.join(RESULTS_DIR, f'oov_{args.name}.json')
    with open(output, 'w') as f:
        json.dump(results, f, indent=2, ensure_ascii=False)
    print(f"\nResults: {output}")


if __name__ == '__main__':
    main()
