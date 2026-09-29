# Dictionary lookup structures for morphologically rich languages

Code, data and measurements behind the paper *Performance Comparison of Data
Structures for Dictionary Lookup in Morphologically Rich Languages* (ICAI 2026,
submission 0926).

Seven structures are implemented twice, once in C and once in Python, and
measured on a Serbian morphological dictionary of 1,859,742 word forms and on an
English word list of 235,976 entries: hash map, sorted array with binary search,
trie, ternary search tree, DAWG, Bloom filter and cuckoo filter. An eighth
configuration, a cuckoo filter with 16-bit fingerprints, was added so that the
two probabilistic filters can be compared at a matched false positive rate.

## Layout

    c/          C implementations and the benchmark driver
    python/     Python implementations and the benchmark driver
    data/       word lists
    results/    measurements, one directory per session
    analysis/   statistics and session aggregation

## Building and running

    cd c && make
    ./benchmark --dataset ../data/srlex_words.txt --name srlex --runs 30 \
                --output ../results/srlex_c.json

    python3 python/benchmark.py --dataset data/srlex_words.txt --name srlex \
                --runs 30 --output results/srlex_py.json

Driver options:

    --runs N          repetitions per structure (default 30)
    --scalability     also measure on subsets of increasing size
    --scenarios       measure three query mixes: all hits, half and half, all misses
    --bloom-fpr R     target false positive rate of the Bloom filter (default 0.01)
    --fp-words N      number of non-words probed when measuring the observed rate

## Experimental protocol

Every structure is measured on the same queries. The driver draws 10,000 query
words once per dataset with seed 42 and passes the same list to each structure,
so the observations are paired across structures. Each structure is rebuilt from
scratch before every repetition.

Timing is sensitive to what else the machine is doing. Measurements are taken in
three separate sessions, the median session is reported, and the spread across
sessions is recorded alongside it. Quantities that must be deterministic, the
memory footprint and the false positive counts, are checked to be identical in
all three sessions; `analysis/aggregate_sessions.py` fails if they are not.

Reference machine: Apple M2, 16 GB, macOS. C compiled with
`cc -O2 -Wall -Wextra -std=c11` and the architecture flag for the host. Python
3.14.

## Analysis

    python3 analysis/recompute_stats.py s1        # paired tests, one session
    python3 analysis/aggregate_sessions.py        # combine s1, s2, s3

`recompute_stats.py` reports medians and interquartile ranges, the Wilcoxon
signed-rank test for all 21 structure pairs with Holm-Bonferroni correction, and
Cliff's delta as an effect size. It also reports the Spearman correlation
between structures across runs, which shows whether pairing by run index carries
any information.

## Data

`data/srlex_words.txt` is the list of distinct word forms extracted from srLex
1.3, the inflectional lexicon of Serbian by Ljubesic, Klubicka, Agic and Jazbec,
distributed by CLARIN.SI at http://hdl.handle.net/11356/1233 under CC BY-SA 4.0.
`data/srlex_sizematched.txt` is a random sample of 235,976 of those forms, drawn
with seed 42, used for the size-matched comparison against English.

`data/english_words.txt` is the Unix word list (Webster's Second International
Dictionary) as shipped at `/usr/share/dict/words`.

## Licence

Code in `c/`, `python/` and `analysis/` is released under the MIT licence, see
LICENSE. The word lists in `data/` derive from srLex 1.3 and remain under
CC BY-SA 4.0; attribution as given above must be preserved.

## Authors

Nikola Vukotic, Slavimir Stosovic
