#!/usr/bin/env python3
"""
Benchmark harness for the seven data structures.

Metrics:
  - build time
  - lookup time, reported as mean, median and standard deviation
  - memory usage
  - false positive rate, for the probabilistic structures

Protocol:
  - 30 repetitions per structure
  - 10,000 lookups per repetition, drawn once with a fixed seed and
    reused for every structure, so all structures see the same queries
  - paired Wilcoxon signed-rank test with Holm correction, see analysis/

Output: a JSON file holding every per-run value, not only the summary.

Authors: Nikola Vukotic, Slavimir Stosovic
"""

import argparse
import json
import os
import random
import string
import sys
import time
import statistics
from pathlib import Path

from structures import ALL_STRUCTURES


# ============================================================
# KONFIGURACIJA
# ============================================================

NUM_RUNS = 30           # broj ponavljanja
NUM_LOOKUPS = 10_000    # broj pretraga po ponavljanju
RANDOM_SEED = 42

# Subset sizes used for the scalability analysis
SCALABILITY_SIZES = [10_000, 50_000, 100_000, 500_000, 1_000_000]


# ============================================================
# DATA LOADING
# ============================================================

def load_words(filepath: str) -> list:
    """Read a word list, one word per line."""
    with open(filepath, 'r', encoding='utf-8') as f:
        words = [line.strip() for line in f if line.strip()]
    return words


def generate_nonexistent_words(existing_set: set, count: int, rng: random.Random) -> list:
    """Generate words absent from the dictionary, used to measure the false positive rate."""
    non_words = []
    chars = string.ascii_lowercase + 'čćžšđ'
    while len(non_words) < count:
        length = rng.randint(3, 15)
        word = ''.join(rng.choice(chars) for _ in range(length))
        if word not in existing_set:
            non_words.append(word)
    return non_words


# ============================================================
# BENCHMARK JEDNE STRUKTURE
# ============================================================

def benchmark_structure(StructureClass, words: list, lookup_words: list,
                        non_words: list, num_runs: int) -> dict:
    """
    Benchmark one structure.

    Returns:
        dict sa svim metrikama
    """
    name = StructureClass.name
    print(f"\n  {name}...")

    build_times = []
    lookup_times = []
    memory_sizes = []
    fp_rates = []

    for run in range(num_runs):
        # Build
        if StructureClass.probabilistic:
            struct = StructureClass(expected_items=len(words))
        else:
            struct = StructureClass()

        t0 = time.perf_counter()
        struct.build(words)
        build_time = time.perf_counter() - t0
        build_times.append(build_time)

        # Lookup
        t0 = time.perf_counter()
        for word in lookup_words:
            struct.lookup(word)
        lookup_time = (time.perf_counter() - t0) / len(lookup_words)
        lookup_times.append(lookup_time)

        # Memory, measured once
        if run == 0:
            mem = struct.memory_usage()
            memory_sizes.append(mem)

        # False positive rate, probabilistic structures only
        if StructureClass.probabilistic and run == 0:
            fp = sum(1 for w in non_words if struct.lookup(w))
            fp_rate = fp / len(non_words) * 100
            fp_rates.append(fp_rate)

        if (run + 1) % 10 == 0:
            print(f"    run {run + 1}/{num_runs}")

    # Summary statistics
    result = {
        'name': name,
        'supports_prefix': StructureClass.supports_prefix,
        'probabilistic': StructureClass.probabilistic,
        'build_time': {
            'mean': statistics.mean(build_times),
            'median': statistics.median(build_times),
            'stdev': statistics.stdev(build_times) if len(build_times) > 1 else 0,
            'min': min(build_times),
            'max': max(build_times),
            'all': build_times,
        },
        'lookup_time_per_query': {
            'mean': statistics.mean(lookup_times),
            'median': statistics.median(lookup_times),
            'stdev': statistics.stdev(lookup_times) if len(lookup_times) > 1 else 0,
            'min': min(lookup_times),
            'max': max(lookup_times),
            'all': lookup_times,
            'unit': 'seconds',
        },
        'memory_bytes': memory_sizes[0] if memory_sizes else 0,
        'memory_mb': round(memory_sizes[0] / (1024 * 1024), 2) if memory_sizes else 0,
    }

    if fp_rates:
        result['fp_rate_percent'] = fp_rates[0]

    # Print a one line summary
    lt_us = result['lookup_time_per_query']['median'] * 1e6
    mem_mb = result['memory_mb']
    bt = result['build_time']['median']
    print(f"    lookup: {lt_us:.2f} μs | memory: {mem_mb:.1f} MB | build: {bt:.3f}s")
    if fp_rates:
        print(f"    FP rate: {fp_rates[0]:.2f}%")

    return result


# ============================================================
# PUNI BENCHMARK
# ============================================================

def run_full_benchmark(words: list, dataset_name: str, num_runs: int = NUM_RUNS) -> dict:
    """Benchmark every structure on one dataset."""

    print(f"\n{'='*60}")
    print(f"BENCHMARK: {dataset_name}")
    print(f"  Words: {len(words):,}")
    print(f"  Ponavljanja: {num_runs}")
    print(f"  Pretraga po ponavljanju: {NUM_LOOKUPS:,}")
    print(f"{'='*60}")

    rng = random.Random(RANDOM_SEED)
    word_set = set(words)

    # Query words are drawn once and reused across structures
    existing_sample = rng.sample(words, min(NUM_LOOKUPS, len(words)))
    non_words = generate_nonexistent_words(word_set, NUM_LOOKUPS, rng)
    lookup_words = existing_sample  # timing is measured on present words

    results = {
        'dataset': dataset_name,
        'num_words': len(words),
        'num_runs': num_runs,
        'num_lookups': NUM_LOOKUPS,
        'avg_word_length': sum(len(w) for w in words) / len(words),
        'structures': []
    }

    for StructureClass in ALL_STRUCTURES:
        try:
            r = benchmark_structure(
                StructureClass, words, lookup_words, non_words, num_runs
            )
            results['structures'].append(r)
        except Exception as e:
            print(f"    ERROR: {e}")
            results['structures'].append({
                'name': StructureClass.name,
                'error': str(e)
            })

    return results


# ============================================================
# ANALIZA SKALABILNOSTI
# ============================================================

def run_scalability_analysis(words: list, dataset_name: str) -> dict:
    """Benchmark on subsets of increasing size."""

    print(f"\n{'='*60}")
    print(f"SKALABILNOST: {dataset_name}")
    print(f"{'='*60}")

    rng = random.Random(RANDOM_SEED)
    sizes = [s for s in SCALABILITY_SIZES if s < len(words)]
    sizes.append(len(words))  # add the full dictionary

    results = {
        'dataset': dataset_name,
        'sizes': [],
    }

    for size in sizes:
        subset = rng.sample(words, min(size, len(words)))
        print(f"\n  Size: {len(subset):,}")

        size_results = {
            'size': len(subset),
            'structures': []
        }

        word_set = set(subset)
        lookup_sample = rng.sample(subset, min(1000, len(subset)))
        non_words = generate_nonexistent_words(word_set, 1000, rng)

        for StructureClass in ALL_STRUCTURES:
            try:
                r = benchmark_structure(
                    StructureClass, subset, lookup_sample, non_words,
                    num_runs=5  # manje ponavljanja za skalabilnost
                )
                size_results['structures'].append(r)
            except Exception as e:
                print(f"    ERROR in {StructureClass.name}: {e}")

        results['sizes'].append(size_results)

    return results


# ============================================================
# MAIN
# ============================================================

def main():
    parser = argparse.ArgumentParser(description='Data structure benchmark')
    parser.add_argument('--dataset', type=str, required=True,
                        help='Path to the word list')
    parser.add_argument('--name', type=str, default='dataset',
                        help='Dataset name')
    parser.add_argument('--runs', type=int, default=NUM_RUNS,
                        help=f'Repetitions per structure (default: {NUM_RUNS})')
    parser.add_argument('--scalability', action='store_true',
                        help='Also run the scalability analysis')
    parser.add_argument('--output', type=str, default=None,
                        help='Output JSON file')

    args = parser.parse_args()

    # Load the word list
    words = load_words(args.dataset)
    print(f"Loaded {len(words):,} words from {args.dataset}")

    # Puni benchmark
    results = run_full_benchmark(words, args.name, args.runs)

    # Skalabilnost
    if args.scalability:
        scale_results = run_scalability_analysis(words, args.name)
        results['scalability'] = scale_results

    # Write the results
    output_path = args.output or f"results_{args.name}.json"
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(results, f, ensure_ascii=False, indent=2, default=str)
    print(f"\nResults written to: {output_path}")


if __name__ == '__main__':
    main()
