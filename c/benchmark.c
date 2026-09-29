/*
 * benchmark.c - Benchmark driver for 7 dictionary lookup data structures.
 *
 * Outputs JSON matching the Python benchmark format.
 *
 * Authors: Nikola Vukotic, Slavimir Stosovic
 * Date: Jun 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include "structures.h"

/* Global memory tracker */
size_t g_mem_allocated = 0;

/* ============================================================
 * Configuration
 * ============================================================ */

#define DEFAULT_NUM_RUNS    30
#define NUM_LOOKUPS         10000
#define NUM_FP_TEST_WORDS   50000

/* Non-words used to estimate the false positive rate. Configurable for the
 * ICAI revision: 50,000 probes cannot resolve a rate near 0.01 percent. */
static size_t g_fp_test_words = NUM_FP_TEST_WORDS;
#define RANDOM_SEED         42

/* Target false positive rate of the Bloom filter. Made configurable for the
 * ICAI revision so that Bloom and cuckoo can be compared at a matched rate;
 * the default reproduces the published configuration. */
static double g_bloom_fpr = 0.01;

static const size_t SCALABILITY_SIZES[] = {10000, 50000, 100000, 500000, 1000000};
#define NUM_SCALE_SIZES 5

/* ============================================================
 * Simple LCG random (deterministic)
 * ============================================================ */

static uint64_t rng_state = RANDOM_SEED;

static void rng_seed(uint64_t s) { rng_state = s; }

static uint64_t rng_next(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return rng_state;
}

static size_t rng_range(size_t n) {
    return (size_t)(rng_next() >> 33) % n;
}

/* ============================================================
 * Data loading
 * ============================================================ */

typedef struct {
    char **words;
    size_t count;
    size_t total_len; /* sum of all word lengths */
} wordlist_t;

static wordlist_t load_words(const char *path) {
    wordlist_t wl = {NULL, 0, 0};

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", path);
        exit(1);
    }

    /* First pass: count lines */
    size_t cap = 1024;
    wl.words = (char **)malloc(cap * sizeof(char *));
    char buf[MAX_WORD_LEN + 16];

    while (fgets(buf, sizeof(buf), f)) {
        /* Strip newline */
        size_t len = strlen(buf);
        while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
            buf[--len] = '\0';
        if (len == 0) continue;

        if (wl.count >= cap) {
            cap *= 2;
            wl.words = (char **)realloc(wl.words, cap * sizeof(char *));
        }
        wl.words[wl.count] = (char *)malloc(len + 1);
        memcpy(wl.words[wl.count], buf, len + 1);
        wl.total_len += len;
        wl.count++;
    }
    fclose(f);
    return wl;
}

static void free_wordlist(wordlist_t *wl) {
    for (size_t i = 0; i < wl->count; i++)
        free(wl->words[i]);
    free(wl->words);
    wl->words = NULL;
    wl->count = 0;
}

/* ============================================================
 * Non-word generation (for FP rate testing)
 * ============================================================ */

static char **generate_nonwords(char **words, size_t n, size_t count) {
    /*
     * Build a simple set (hash table) to check membership,
     * then generate random strings not in set.
     */
    /* Simple hash set for existing words */
    size_t set_cap = 1;
    while (set_cap < n * 2) set_cap <<= 1;
    char **set = (char **)calloc(set_cap, sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        uint64_t h = fnv1a_hash(words[i], strlen(words[i]));
        size_t idx = h & (set_cap - 1);
        while (set[idx]) {
            if (strcmp(set[idx], words[i]) == 0) break;
            idx = (idx + 1) & (set_cap - 1);
        }
        set[idx] = words[i];
    }

    char **nonwords = (char **)malloc(count * sizeof(char *));
    /* chars: a-z plus some accented chars for Serbian */
    const char chars[] = "abcdefghijklmnopqrstuvwxyz";
    int nchars = 26;
    size_t generated = 0;

    while (generated < count) {
        size_t len = 3 + rng_range(13); /* 3-15 chars */
        char *w = (char *)malloc(len + 1);
        for (size_t j = 0; j < len; j++)
            w[j] = chars[rng_range(nchars)];
        w[len] = '\0';

        /* Check not in set */
        uint64_t h = fnv1a_hash(w, len);
        size_t idx = h & (set_cap - 1);
        bool found = false;
        while (set[idx]) {
            if (strcmp(set[idx], w) == 0) { found = true; break; }
            idx = (idx + 1) & (set_cap - 1);
        }
        if (!found) {
            nonwords[generated++] = w;
        } else {
            free(w);
        }
    }
    free(set);
    return nonwords;
}

/* ============================================================
 * Statistics helpers
 * ============================================================ */

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return (da > db) - (da < db);
}

typedef struct {
    double mean, median, stdev, min, max;
    double *all;
    int n;
} stats_t;

static stats_t compute_stats(double *vals, int n) {
    stats_t s;
    s.all = vals;
    s.n = n;

    double sum = 0;
    s.min = vals[0]; s.max = vals[0];
    for (int i = 0; i < n; i++) {
        sum += vals[i];
        if (vals[i] < s.min) s.min = vals[i];
        if (vals[i] > s.max) s.max = vals[i];
    }
    s.mean = sum / n;

    double var = 0;
    for (int i = 0; i < n; i++) {
        double d = vals[i] - s.mean;
        var += d * d;
    }
    s.stdev = (n > 1) ? sqrt(var / (n - 1)) : 0;

    /* Median: sort a copy */
    double *tmp = (double *)malloc(n * sizeof(double));
    memcpy(tmp, vals, n * sizeof(double));
    qsort(tmp, n, sizeof(double), cmp_double);
    if (n % 2 == 0)
        s.median = (tmp[n/2 - 1] + tmp[n/2]) / 2.0;
    else
        s.median = tmp[n/2];
    free(tmp);
    return s;
}

/* ============================================================
 * JSON output helpers
 * ============================================================ */

static void json_write_double_array(FILE *f, const double *arr, int n) {
    fprintf(f, "[");
    for (int i = 0; i < n; i++) {
        fprintf(f, "%.9e", arr[i]);
        if (i < n - 1) fprintf(f, ", ");
    }
    fprintf(f, "]");
}

static void json_write_stats(FILE *f, const char *name, stats_t *s,
                              const char *unit, int indent) {
    char pad[64];
    memset(pad, ' ', indent); pad[indent] = '\0';

    fprintf(f, "%s\"%s\": {\n", pad, name);
    fprintf(f, "%s  \"mean\": %.9e,\n", pad, s->mean);
    fprintf(f, "%s  \"median\": %.9e,\n", pad, s->median);
    fprintf(f, "%s  \"stdev\": %.9e,\n", pad, s->stdev);
    fprintf(f, "%s  \"min\": %.9e,\n", pad, s->min);
    fprintf(f, "%s  \"max\": %.9e,\n", pad, s->max);
    fprintf(f, "%s  \"all\": ", pad);
    json_write_double_array(f, s->all, s->n);
    if (unit) {
        fprintf(f, ",\n%s  \"unit\": \"%s\"\n", pad, unit);
    } else {
        fprintf(f, "\n");
    }
    fprintf(f, "%s}", pad);
}

/* ============================================================
 * Structure identifiers
 * ============================================================ */

typedef enum {
    STRUCT_HASHMAP = 0,
    STRUCT_SORTED_ARRAY,
    STRUCT_TRIE,
    STRUCT_TST,
    STRUCT_DAWG,
    STRUCT_BLOOM,
    STRUCT_CUCKOO,
    STRUCT_CUCKOO16,
    STRUCT_COUNT
} struct_id_t;

static const char *struct_names[] = {
    "HashMap",
    "Sorted Array + Binary Search",
    "Trie",
    "Ternary Search Tree",
    "DAWG",
    "Bloom Filter",
    "Cuckoo Filter",
    "Cuckoo Filter 16-bit"
};

static const bool struct_probabilistic[] = {
    false, false, false, false, false, true, true, true
};

static const bool struct_supports_prefix[] = {
    false, false, true, true, true, false, false, false
};

/* ============================================================
 * Benchmark one structure
 * ============================================================ */

typedef struct {
    const char *name;
    bool supports_prefix;
    bool probabilistic;
    stats_t build_time;
    stats_t lookup_time;
    size_t memory_bytes;
    double fp_rate_percent; /* -1 if not applicable */
    size_t fp_count;        /* raw counter behind fp_rate_percent */
    size_t fp_tested;       /* how many non-words were probed */
    bool has_error;
    char error_msg[256];
} bench_result_t;

static bench_result_t benchmark_one(struct_id_t sid, char **words, size_t n,
                                      char **lookup_words, size_t nlookup,
                                      char **nonwords, size_t nnonwords,
                                      int num_runs) {
    bench_result_t res;
    memset(&res, 0, sizeof(res));
    res.name = struct_names[sid];
    res.supports_prefix = struct_supports_prefix[sid];
    res.probabilistic = struct_probabilistic[sid];
    res.fp_rate_percent = -1;
    res.has_error = false;

    double *build_times = (double *)malloc(num_runs * sizeof(double));
    double *lookup_times = (double *)malloc(num_runs * sizeof(double));
    size_t mem = 0;

    printf("\n  %s...\n", res.name);

    for (int run = 0; run < num_runs; run++) {
        double t0, t1;

        /* Pre-compute lookup word lengths */
        size_t *lookup_lens = NULL;
        if (run == 0) {
            lookup_lens = (size_t *)malloc(nlookup * sizeof(size_t));
            for (size_t i = 0; i < nlookup; i++)
                lookup_lens[i] = strlen(lookup_words[i]);
        }

        switch (sid) {
        case STRUCT_HASHMAP: {
            hashmap_t hm;
            t0 = get_time_sec();
            hashmap_init(&hm, n);
            hashmap_build(&hm, words, n);
            t1 = get_time_sec();
            build_times[run] = t1 - t0;

            /* Lookup */
            t0 = get_time_sec();
            for (size_t i = 0; i < nlookup; i++)
                hashmap_lookup(&hm, lookup_words[i], strlen(lookup_words[i]));
            t1 = get_time_sec();
            lookup_times[run] = (t1 - t0) / nlookup;

            if (run == 0) mem = hashmap_memory(&hm);
            hashmap_free(&hm);
            break;
        }
        case STRUCT_SORTED_ARRAY: {
            sorted_array_t sa;
            t0 = get_time_sec();
            sorted_array_init(&sa);
            sorted_array_build(&sa, words, n);
            t1 = get_time_sec();
            build_times[run] = t1 - t0;

            t0 = get_time_sec();
            for (size_t i = 0; i < nlookup; i++)
                sorted_array_lookup(&sa, lookup_words[i]);
            t1 = get_time_sec();
            lookup_times[run] = (t1 - t0) / nlookup;

            if (run == 0) mem = sorted_array_memory(&sa);
            sorted_array_free(&sa);
            break;
        }
        case STRUCT_TRIE: {
            trie_t tr;
            /* Estimate max nodes: avg_word_len * n is upper bound,
               but many prefixes shared. Use n*10 as safe estimate. */
            size_t max_nodes = n * 10;
            if (max_nodes < 100000) max_nodes = 100000;

            t0 = get_time_sec();
            trie_init(&tr, max_nodes);
            trie_build(&tr, words, n);
            t1 = get_time_sec();
            build_times[run] = t1 - t0;

            t0 = get_time_sec();
            for (size_t i = 0; i < nlookup; i++)
                trie_lookup(&tr, lookup_words[i], strlen(lookup_words[i]));
            t1 = get_time_sec();
            lookup_times[run] = (t1 - t0) / nlookup;

            if (run == 0) mem = trie_memory(&tr);
            trie_free(&tr);
            break;
        }
        case STRUCT_TST: {
            tst_t tst;
            /* Each word char = one node at most; shared prefixes reduce this */
            size_t max_nodes = n * 15;
            if (max_nodes < 100000) max_nodes = 100000;

            t0 = get_time_sec();
            tst_init(&tst, max_nodes);
            tst_build(&tst, words, n);
            t1 = get_time_sec();
            build_times[run] = t1 - t0;

            t0 = get_time_sec();
            for (size_t i = 0; i < nlookup; i++)
                tst_lookup(&tst, lookup_words[i], strlen(lookup_words[i]));
            t1 = get_time_sec();
            lookup_times[run] = (t1 - t0) / nlookup;

            if (run == 0) mem = tst_memory(&tst);
            tst_free(&tst);
            break;
        }
        case STRUCT_DAWG: {
            dawg_t dg;
            size_t max_nodes = n * 10;
            size_t max_children = n * 10;
            if (max_nodes < 100000) max_nodes = 100000;
            if (max_children < 100000) max_children = 100000;

            t0 = get_time_sec();
            dawg_init(&dg, max_nodes, max_children);
            dawg_build(&dg, words, n);
            t1 = get_time_sec();
            build_times[run] = t1 - t0;

            t0 = get_time_sec();
            for (size_t i = 0; i < nlookup; i++)
                dawg_lookup(&dg, lookup_words[i], strlen(lookup_words[i]));
            t1 = get_time_sec();
            lookup_times[run] = (t1 - t0) / nlookup;

            if (run == 0) mem = dawg_memory(&dg);
            dawg_free(&dg);
            break;
        }
        case STRUCT_BLOOM: {
            bloom_t bl;
            t0 = get_time_sec();
            bloom_init(&bl, n, g_bloom_fpr);
            bloom_build(&bl, words, n);
            t1 = get_time_sec();
            build_times[run] = t1 - t0;

            t0 = get_time_sec();
            for (size_t i = 0; i < nlookup; i++)
                bloom_lookup(&bl, lookup_words[i], strlen(lookup_words[i]));
            t1 = get_time_sec();
            lookup_times[run] = (t1 - t0) / nlookup;

            if (run == 0) {
                mem = bloom_memory(&bl);
                /* FP rate */
                size_t fp = 0;
                for (size_t i = 0; i < nnonwords; i++) {
                    if (bloom_lookup(&bl, nonwords[i], strlen(nonwords[i])))
                        fp++;
                }
                res.fp_rate_percent = (double)fp / nnonwords * 100.0;
                res.fp_count = fp;
                res.fp_tested = nnonwords;
            }
            bloom_free(&bl);
            break;
        }
        case STRUCT_CUCKOO: {
            cuckoo_t ck;
            t0 = get_time_sec();
            cuckoo_init(&ck, n);
            cuckoo_build(&ck, words, n);
            t1 = get_time_sec();
            build_times[run] = t1 - t0;

            t0 = get_time_sec();
            for (size_t i = 0; i < nlookup; i++)
                cuckoo_lookup(&ck, lookup_words[i], strlen(lookup_words[i]));
            t1 = get_time_sec();
            lookup_times[run] = (t1 - t0) / nlookup;

            if (run == 0) {
                mem = cuckoo_memory(&ck);
                size_t fp = 0;
                for (size_t i = 0; i < nnonwords; i++) {
                    if (cuckoo_lookup(&ck, nonwords[i], strlen(nonwords[i])))
                        fp++;
                }
                res.fp_rate_percent = (double)fp / nnonwords * 100.0;
                res.fp_count = fp;
                res.fp_tested = nnonwords;
            }
            cuckoo_free(&ck);
            break;
        }
        case STRUCT_CUCKOO16: {
            cuckoo16_t ck;
            t0 = get_time_sec();
            cuckoo16_init(&ck, n);
            cuckoo16_build(&ck, words, n);
            t1 = get_time_sec();
            build_times[run] = t1 - t0;

            t0 = get_time_sec();
            for (size_t i = 0; i < nlookup; i++)
                cuckoo16_lookup(&ck, lookup_words[i], strlen(lookup_words[i]));
            t1 = get_time_sec();
            lookup_times[run] = (t1 - t0) / nlookup;

            if (run == 0) {
                mem = cuckoo16_memory(&ck);
                size_t fp = 0;
                for (size_t i = 0; i < nnonwords; i++) {
                    if (cuckoo16_lookup(&ck, nonwords[i], strlen(nonwords[i])))
                        fp++;
                }
                res.fp_rate_percent = (double)fp / nnonwords * 100.0;
                res.fp_count = fp;
                res.fp_tested = nnonwords;
            }
            cuckoo16_free(&ck);
            break;
        }
        default: break;
        }

        if (lookup_lens) free(lookup_lens);

        if ((run + 1) % 10 == 0)
            printf("    run %d/%d\n", run + 1, num_runs);
    }

    res.build_time = compute_stats(build_times, num_runs);
    res.lookup_time = compute_stats(lookup_times, num_runs);
    res.memory_bytes = mem;

    double lt_us = res.lookup_time.median * 1e6;
    double mem_mb = (double)mem / (1024.0 * 1024.0);
    printf("    lookup: %.2f us | memory: %.1f MB | build: %.3fs\n",
           lt_us, mem_mb, res.build_time.median);
    if (res.fp_rate_percent >= 0)
        printf("    FP rate: %.5f%% (%zu / %zu)\n",
               res.fp_rate_percent, res.fp_count, res.fp_tested);

    return res;
}

/* ============================================================
 * Write results to JSON
 * ============================================================ */

static void write_structure_json(FILE *f, bench_result_t *r, int indent) {
    char pad[64];
    memset(pad, ' ', indent); pad[indent] = '\0';

    fprintf(f, "%s{\n", pad);
    fprintf(f, "%s  \"name\": \"%s\",\n", pad, r->name);
    fprintf(f, "%s  \"supports_prefix\": %s,\n", pad, r->supports_prefix ? "true" : "false");
    fprintf(f, "%s  \"probabilistic\": %s,\n", pad, r->probabilistic ? "true" : "false");

    json_write_stats(f, "build_time", &r->build_time, NULL, indent + 2);
    fprintf(f, ",\n");
    json_write_stats(f, "lookup_time_per_query", &r->lookup_time, "seconds", indent + 2);
    fprintf(f, ",\n");

    fprintf(f, "%s  \"memory_bytes\": %zu,\n", pad, r->memory_bytes);
    fprintf(f, "%s  \"memory_mb\": %.2f", pad, (double)r->memory_bytes / (1024.0 * 1024.0));

    if (r->fp_rate_percent >= 0) {
        fprintf(f, ",\n%s  \"fp_rate_percent\": %.5f,\n", pad, r->fp_rate_percent);
        fprintf(f, "%s  \"fp_count\": %zu,\n", pad, r->fp_count);
        fprintf(f, "%s  \"fp_tested\": %zu\n", pad, r->fp_tested);
    } else {
        fprintf(f, "\n");
    }
    fprintf(f, "%s}", pad);
}

/* ============================================================
 * Full benchmark
 * ============================================================ */

static void run_full_benchmark(char **words, size_t n, const char *dataset_name,
                                int num_runs, FILE *outf, bool write_comma_before) {

    printf("\n============================================================\n");
    printf("BENCHMARK: %s\n", dataset_name);
    printf("  Words: %zu\n", n);
    printf("  Runs: %d\n", num_runs);
    printf("  Lookups per run: %d\n", NUM_LOOKUPS);
    printf("============================================================\n");

    /* Prepare lookup words */
    rng_seed(RANDOM_SEED);
    size_t nlookup = NUM_LOOKUPS < n ? NUM_LOOKUPS : n;
    char **lookup_words = (char **)malloc(nlookup * sizeof(char *));

    /* Sample without replacement using Fisher-Yates partial shuffle */
    size_t *indices = (size_t *)malloc(n * sizeof(size_t));
    for (size_t i = 0; i < n; i++) indices[i] = i;
    for (size_t i = 0; i < nlookup; i++) {
        size_t j = i + rng_range(n - i);
        size_t tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
        lookup_words[i] = words[indices[i]];
    }
    free(indices);

    /* Generate nonwords */
    char **nonwords = generate_nonwords(words, n, g_fp_test_words);

    /* Compute avg word length */
    double avg_len = 0;
    for (size_t i = 0; i < n; i++) avg_len += strlen(words[i]);
    avg_len /= n;

    /* Run benchmarks */
    bench_result_t results[STRUCT_COUNT];
    for (int s = 0; s < STRUCT_COUNT; s++) {
        results[s] = benchmark_one((struct_id_t)s, words, n,
                                    lookup_words, nlookup,
                                    nonwords, g_fp_test_words, num_runs);
    }

    /* Write JSON */
    if (write_comma_before) fprintf(outf, ",\n");

    fprintf(outf, "  \"dataset\": \"%s\",\n", dataset_name);
    fprintf(outf, "  \"num_words\": %zu,\n", n);
    fprintf(outf, "  \"num_runs\": %d,\n", num_runs);
    fprintf(outf, "  \"num_lookups\": %d,\n", NUM_LOOKUPS);
    fprintf(outf, "  \"avg_word_length\": %.6f,\n", avg_len);
    fprintf(outf, "  \"structures\": [\n");

    for (int s = 0; s < STRUCT_COUNT; s++) {
        write_structure_json(outf, &results[s], 4);
        if (s < STRUCT_COUNT - 1) fprintf(outf, ",\n");
        else fprintf(outf, "\n");
    }
    fprintf(outf, "  ]");

    /* Cleanup */
    for (int s = 0; s < STRUCT_COUNT; s++) {
        free(results[s].build_time.all);
        free(results[s].lookup_time.all);
    }
    free(lookup_words);
    for (size_t i = 0; i < g_fp_test_words; i++)
        free(nonwords[i]);
    free(nonwords);
}

/* ============================================================
 * Scalability analysis
 * ============================================================ */

static void run_scalability(char **words, size_t n, const char *dataset_name,
                             FILE *outf) {

    printf("\n============================================================\n");
    printf("SCALABILITY: %s\n", dataset_name);
    printf("============================================================\n");

    fprintf(outf, ",\n  \"scalability\": {\n");
    fprintf(outf, "    \"dataset\": \"%s\",\n", dataset_name);
    fprintf(outf, "    \"sizes\": [\n");

    /* Determine sizes */
    size_t sizes[NUM_SCALE_SIZES + 1];
    int nsizes = 0;
    for (int i = 0; i < NUM_SCALE_SIZES; i++) {
        if (SCALABILITY_SIZES[i] < n)
            sizes[nsizes++] = SCALABILITY_SIZES[i];
    }
    sizes[nsizes++] = n; /* full size */

    rng_seed(RANDOM_SEED);

    for (int si = 0; si < nsizes; si++) {
        size_t sz = sizes[si];
        printf("\n  Size: %zu\n", sz);

        /* Sample subset */
        char **subset = (char **)malloc(sz * sizeof(char *));
        size_t *perm = (size_t *)malloc(n * sizeof(size_t));
        for (size_t i = 0; i < n; i++) perm[i] = i;
        for (size_t i = 0; i < sz; i++) {
            size_t j = i + rng_range(n - i);
            size_t tmp = perm[i]; perm[i] = perm[j]; perm[j] = tmp;
            subset[i] = words[perm[i]];
        }
        free(perm);

        /* Lookup sample */
        size_t nlookup = 1000 < sz ? 1000 : sz;
        char **lookup_words = (char **)malloc(nlookup * sizeof(char *));
        for (size_t i = 0; i < nlookup; i++)
            lookup_words[i] = subset[rng_range(sz)];

        char **nonwords = generate_nonwords(subset, sz, 1000);

        /* Run */
        fprintf(outf, "      {\n        \"size\": %zu,\n        \"structures\": [\n", sz);

        for (int s = 0; s < STRUCT_COUNT; s++) {
            bench_result_t r = benchmark_one((struct_id_t)s, subset, sz,
                                              lookup_words, nlookup,
                                              nonwords, 1000, 5);
            write_structure_json(outf, &r, 10);
            if (s < STRUCT_COUNT - 1) fprintf(outf, ",\n");
            else fprintf(outf, "\n");
            free(r.build_time.all);
            free(r.lookup_time.all);
        }

        fprintf(outf, "        ]\n      }");
        if (si < nsizes - 1) fprintf(outf, ",\n");
        else fprintf(outf, "\n");

        free(subset);
        free(lookup_words);
        for (size_t i = 0; i < 1000; i++) free(nonwords[i]);
        free(nonwords);
    }

    fprintf(outf, "    ]\n  }");
}

/* ============================================================
 * Main
 * ============================================================ */

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s --dataset <file> --name <name> [--runs N] "
            "[--scalability] [--output results.json]\n", prog);
    exit(1);
}


/* ============================================================
 * Query scenarios (ICAI revision)
 *
 * Reviewer 2 asked that the fail-fast claim be supported in the
 * experimental section, and it had only been measured in Python.
 * This mode measures lookup time for three query mixes in C:
 * all hits, half hits and half misses, and all misses.
 *
 * Out-of-vocabulary words are produced the same way as in the
 * Python harness: a real word is altered by one of four edits,
 * and the result is kept only if it is not itself in the
 * dictionary. Edits respect UTF-8 character boundaries, so a
 * mutated Serbian word remains well formed.
 * ============================================================ */

typedef struct { char **slots; size_t cap; } wordset_t;

static wordset_t wordset_build(char **words, size_t n) {
    wordset_t s;
    s.cap = 1;
    while (s.cap < n * 2) s.cap <<= 1;
    s.slots = (char **)calloc(s.cap, sizeof(char *));
    for (size_t i = 0; i < n; i++) {
        uint64_t h = fnv1a_hash(words[i], strlen(words[i]));
        size_t idx = h & (s.cap - 1);
        while (s.slots[idx] && strcmp(s.slots[idx], words[i]) != 0)
            idx = (idx + 1) & (s.cap - 1);
        s.slots[idx] = words[i];
    }
    return s;
}

static bool wordset_has(const wordset_t *s, const char *w) {
    uint64_t h = fnv1a_hash(w, strlen(w));
    size_t idx = h & (s->cap - 1);
    while (s->slots[idx]) {
        if (strcmp(s->slots[idx], w) == 0) return true;
        idx = (idx + 1) & (s->cap - 1);
    }
    return false;
}

/* Byte offsets at which a UTF-8 character starts, plus the terminator. */
static size_t utf8_bounds(const char *w, size_t len, size_t *out, size_t max) {
    size_t k = 0;
    for (size_t i = 0; i < len && k < max; i++)
        if (((unsigned char)w[i] & 0xC0) != 0x80) out[k++] = i;
    if (k < max) out[k++] = len;
    return k;
}

static char **generate_oov_mutations(char **words, size_t n, size_t count,
                                      const wordset_t *set) {
    const char letters[] = "abcdefghijklmnopqrstuvwxyz";
    char **oov = (char **)malloc(count * sizeof(char *));
    size_t made = 0;
    char buf[512];
    size_t bounds[256];

    while (made < count) {
        const char *base = words[rng_range(n)];
        size_t blen = strlen(base);
        if (blen + 2 >= sizeof(buf)) continue;
        size_t nb = utf8_bounds(base, blen, bounds, 256);
        if (nb < 3) continue;                 /* need at least two characters */
        size_t nchars = nb - 1;
        int kind = (int)rng_range(4);
        size_t ci = rng_range(nchars);
        size_t at = bounds[ci];
        size_t clen = bounds[ci + 1] - at;
        size_t out = 0;

        if (kind == 0) {                      /* replace one character */
            memcpy(buf, base, at); out = at;
            buf[out++] = letters[rng_range(26)];
            memcpy(buf + out, base + at + clen, blen - at - clen);
            out += blen - at - clen;
        } else if (kind == 1) {               /* insert one character */
            memcpy(buf, base, at); out = at;
            buf[out++] = letters[rng_range(26)];
            memcpy(buf + out, base + at, blen - at);
            out += blen - at;
        } else if (kind == 2) {               /* delete one character */
            if (nchars < 4) continue;
            memcpy(buf, base, at); out = at;
            memcpy(buf + out, base + at + clen, blen - at - clen);
            out += blen - at - clen;
        } else {                              /* swap two characters */
            if (ci + 2 > nchars) continue;
            size_t at2 = bounds[ci + 1], clen2 = bounds[ci + 2] - at2;
            memcpy(buf, base, at); out = at;
            memcpy(buf + out, base + at2, clen2); out += clen2;
            memcpy(buf + out, base + at, clen);  out += clen;
            memcpy(buf + out, base + at2 + clen2, blen - at2 - clen2);
            out += blen - at2 - clen2;
        }
        buf[out] = '\0';
        if (out == 0 || wordset_has(set, buf)) continue;

        oov[made] = (char *)malloc(out + 1);
        memcpy(oov[made], buf, out + 1);
        made++;
    }
    return oov;
}

static void run_scenarios(char **words, size_t n, const char *dataset_name,
                           FILE *outf, int num_runs) {
    static const char *scen_names[3] = {"hit_100", "mix_50_50", "miss_100"};

    printf("\n============================================================\n");
    printf("QUERY SCENARIOS: %s\n", dataset_name);
    printf("============================================================\n");

    rng_seed(RANDOM_SEED);
    size_t nlookup = NUM_LOOKUPS < n ? NUM_LOOKUPS : n;

    char **hits = (char **)malloc(nlookup * sizeof(char *));
    for (size_t i = 0; i < nlookup; i++) hits[i] = words[rng_range(n)];

    wordset_t set = wordset_build(words, n);
    char **miss = generate_oov_mutations(words, n, nlookup, &set);

    char **mix = (char **)malloc(nlookup * sizeof(char *));
    for (size_t i = 0; i < nlookup; i++)
        mix[i] = (i % 2 == 0) ? hits[i] : miss[i];

    char **queries[3] = {hits, mix, miss};

    fprintf(outf, "  \"dataset\": \"%s\",\n", dataset_name);
    fprintf(outf, "  \"num_words\": %zu,\n", n);
    fprintf(outf, "  \"num_runs\": %d,\n", num_runs);
    fprintf(outf, "  \"num_lookups\": %zu,\n", nlookup);
    fprintf(outf, "  \"oov_generation\": \"mutation of dictionary words: replace, insert, delete or swap one character, rejected if present in the dictionary\",\n");
    fprintf(outf, "  \"scenarios\": {\n");

    for (int sc = 0; sc < 3; sc++) {
        printf("\n--- scenario: %s ---\n", scen_names[sc]);
        fprintf(outf, "    \"%s\": [\n", scen_names[sc]);
        for (int s = 0; s < STRUCT_COUNT; s++) {
            bench_result_t r = benchmark_one((struct_id_t)s, words, n,
                                             queries[sc], nlookup,
                                             miss, nlookup, num_runs);
            write_structure_json(outf, &r, 6);
            fprintf(outf, s < STRUCT_COUNT - 1 ? ",\n" : "\n");
            free(r.build_time.all);
            free(r.lookup_time.all);
        }
        fprintf(outf, "    ]%s\n", sc < 2 ? "," : "");
    }
    fprintf(outf, "  }");

    for (size_t i = 0; i < nlookup; i++) free(miss[i]);
    free(miss); free(hits); free(mix); free(set.slots);
}

int main(int argc, char *argv[]) {
    const char *dataset_path = NULL;
    const char *dataset_name = "dataset";
    const char *output_path = NULL;
    int num_runs = DEFAULT_NUM_RUNS;
    bool do_scalability = false;
    bool do_scenarios = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--dataset") == 0 && i + 1 < argc)
            dataset_path = argv[++i];
        else if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
            dataset_name = argv[++i];
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc)
            num_runs = atoi(argv[++i]);
        else if (strcmp(argv[i], "--scalability") == 0)
            do_scalability = true;
        else if (strcmp(argv[i], "--scenarios") == 0)
            do_scenarios = true;
        else if (strcmp(argv[i], "--bloom-fpr") == 0 && i + 1 < argc)
            g_bloom_fpr = atof(argv[++i]);
        else if (strcmp(argv[i], "--fp-words") == 0 && i + 1 < argc)
            g_fp_test_words = (size_t)atol(argv[++i]);
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            output_path = argv[++i];
        else
            usage(argv[0]);
    }

    if (!dataset_path) usage(argv[0]);

    /* Set default output */
    char default_output[256];
    if (!output_path) {
        snprintf(default_output, sizeof(default_output), "results_%s.json", dataset_name);
        output_path = default_output;
    }

    /* Load words */
    wordlist_t wl = load_words(dataset_path);
    printf("Loaded %zu words from %s\n", wl.count, dataset_path);

    /* Open output file */
    FILE *outf = fopen(output_path, "w");
    if (!outf) {
        fprintf(stderr, "Error: cannot write to %s\n", output_path);
        exit(1);
    }

    fprintf(outf, "{\n");

    if (do_scenarios) {
        /* Query-mix mode: replaces the standard run, since it reports the
         * same structures under three different query mixes. */
        run_scenarios(wl.words, wl.count, dataset_name, outf, num_runs);
    } else {
        /* Full benchmark */
        run_full_benchmark(wl.words, wl.count, dataset_name, num_runs, outf, false);

        /* Scalability */
        if (do_scalability) {
            run_scalability(wl.words, wl.count, dataset_name, outf);
        }
    }

    fprintf(outf, "\n}\n");
    fclose(outf);
    printf("\nResults saved: %s\n", output_path);

    free_wordlist(&wl);
    return 0;
}
