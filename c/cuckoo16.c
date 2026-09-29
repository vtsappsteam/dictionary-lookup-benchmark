/*
 * cuckoo16.c - Cuckoo filter with 4-way buckets and 16-bit fingerprints.
 *
 * Added for the ICAI revision. Reviewer 1 asked that the two probabilistic
 * filters be compared at a matched false positive rate; the 8-bit filter in
 * cuckoo.c cannot go below roughly 3 percent, so this variant provides a
 * second operating point that a Bloom filter can be configured to match.
 * The logic is identical to cuckoo.c apart from the fingerprint width.
 */

#include <stdlib.h>
#include <string.h>
#include "structures.h"

void cuckoo16_init(cuckoo16_t *c, size_t n) {
    size_t nb = (size_t)((double)n / CUCKOO_BUCKET_SIZE * 1.05);
    if (nb < 64) nb = 64;
    size_t p = 1;
    while (p < nb) p <<= 1;
    c->num_buckets = p;
    c->buckets = (cuckoo16_bucket_t *)calloc(c->num_buckets, sizeof(cuckoo16_bucket_t));
    c->mem_bytes = c->num_buckets * sizeof(cuckoo16_bucket_t);
}

static inline uint16_t cuckoo16_fingerprint(const char *word, size_t len) {
    uint64_t h = fnv1a_hash(word, len);
    uint16_t fp = (uint16_t)(h & 0xFFFF);
    if (fp == 0) fp = 1;
    return fp;
}

static inline size_t cuckoo16_index1(const char *word, size_t len, size_t mask) {
    uint64_t h = fnv1a_hash(word, len);
    return (size_t)(h >> 16) & mask;
}

static inline size_t cuckoo16_alt_index(size_t i1, uint16_t fp, size_t mask) {
    uint64_t fp_hash = (uint64_t)fp * 0x5bd1e995;
    return (i1 ^ (size_t)fp_hash) & mask;
}

static bool cuckoo16_bucket_insert(cuckoo16_bucket_t *bucket, uint16_t fp) {
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        if (bucket->fps[i] == 0) { bucket->fps[i] = fp; return true; }
    }
    return false;
}

static bool cuckoo16_bucket_contains(const cuckoo16_bucket_t *bucket, uint16_t fp) {
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++)
        if (bucket->fps[i] == fp) return true;
    return false;
}

static uint64_t ck16_rng = 0x9e3779b97f4a7c15ULL;
static inline uint64_t ck16_next(void) {
    ck16_rng ^= ck16_rng << 13;
    ck16_rng ^= ck16_rng >> 7;
    ck16_rng ^= ck16_rng << 17;
    return ck16_rng;
}

static bool cuckoo16_insert_fp(cuckoo16_t *c, uint16_t fp, size_t i1) {
    size_t mask = c->num_buckets - 1;
    size_t i2 = cuckoo16_alt_index(i1, fp, mask);
    if (cuckoo16_bucket_insert(&c->buckets[i1], fp)) return true;
    if (cuckoo16_bucket_insert(&c->buckets[i2], fp)) return true;

    size_t idx = (ck16_next() & 1) ? i1 : i2;
    for (int k = 0; k < CUCKOO_MAX_KICKS; k++) {
        int slot = (int)(ck16_next() % CUCKOO_BUCKET_SIZE);
        uint16_t evicted = c->buckets[idx].fps[slot];
        c->buckets[idx].fps[slot] = fp;
        fp = evicted;
        idx = cuckoo16_alt_index(idx, fp, mask);
        if (cuckoo16_bucket_insert(&c->buckets[idx], fp)) return true;
    }
    return false;
}

void cuckoo16_build(cuckoo16_t *c, char **words, size_t n) {
    size_t mask = c->num_buckets - 1;
    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(words[i]);
        uint16_t fp = cuckoo16_fingerprint(words[i], len);
        size_t i1 = cuckoo16_index1(words[i], len, mask);
        if (!cuckoo16_insert_fp(c, fp, i1)) {
            /* Table full: grow and rebuild, same policy as the 8-bit filter. */
            size_t new_buckets = c->num_buckets * 2;
            free(c->buckets);
            c->num_buckets = new_buckets;
            c->buckets = (cuckoo16_bucket_t *)calloc(c->num_buckets,
                                                     sizeof(cuckoo16_bucket_t));
            c->mem_bytes = c->num_buckets * sizeof(cuckoo16_bucket_t);
            mask = c->num_buckets - 1;
            i = (size_t)-1;
        }
    }
}

bool cuckoo16_lookup(const cuckoo16_t *c, const char *word, size_t len) {
    size_t mask = c->num_buckets - 1;
    uint16_t fp = cuckoo16_fingerprint(word, len);
    size_t i1 = cuckoo16_index1(word, len, mask);
    if (cuckoo16_bucket_contains(&c->buckets[i1], fp)) return true;
    size_t i2 = cuckoo16_alt_index(i1, fp, mask);
    return cuckoo16_bucket_contains(&c->buckets[i2], fp);
}

size_t cuckoo16_memory(const cuckoo16_t *c) { return c->mem_bytes; }

void cuckoo16_free(cuckoo16_t *c) { free(c->buckets); c->buckets = NULL; }
