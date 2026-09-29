/*
 * cuckoo.c - Cuckoo filter: 4-way buckets, 8-bit fingerprints.
 */

#include <stdlib.h>
#include <string.h>
#include "structures.h"

void cuckoo_init(cuckoo_t *c, size_t n) {
    /* num_buckets = ceil(n / BUCKET_SIZE * 1.05), rounded up to power of 2 */
    size_t nb = (size_t)((double)n / CUCKOO_BUCKET_SIZE * 1.05);
    if (nb < 64) nb = 64;
    /* Round to power of 2 */
    size_t p = 1;
    while (p < nb) p <<= 1;
    c->num_buckets = p;

    c->buckets = (cuckoo_bucket_t *)calloc(c->num_buckets, sizeof(cuckoo_bucket_t));
    c->mem_bytes = c->num_buckets * sizeof(cuckoo_bucket_t);
}

static inline uint8_t cuckoo_fingerprint(const char *word, size_t len) {
    uint64_t h = fnv1a_hash(word, len);
    uint8_t fp = (uint8_t)(h & 0xFF);
    if (fp == 0) fp = 1; /* 0 means empty slot */
    return fp;
}

static inline size_t cuckoo_index1(const char *word, size_t len, size_t mask) {
    uint64_t h = fnv1a_hash(word, len);
    /* Use upper bits for index (lower used for fingerprint) */
    return (size_t)(h >> 8) & mask;
}

static inline size_t cuckoo_alt_index(size_t i1, uint8_t fp, size_t mask) {
    /* i2 = i1 XOR hash(fp) */
    uint64_t fp_hash = (uint64_t)fp * 0x5bd1e995;
    return (i1 ^ (size_t)fp_hash) & mask;
}

static bool cuckoo_bucket_insert(cuckoo_bucket_t *bucket, uint8_t fp) {
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        if (bucket->fps[i] == 0) {
            bucket->fps[i] = fp;
            return true;
        }
    }
    return false;
}

static bool cuckoo_bucket_contains(const cuckoo_bucket_t *bucket, uint8_t fp) {
    for (int i = 0; i < CUCKOO_BUCKET_SIZE; i++) {
        if (bucket->fps[i] == fp)
            return true;
    }
    return false;
}

static bool cuckoo_insert_fp(cuckoo_t *c, uint8_t fp, size_t i1) {
    size_t mask = c->num_buckets - 1;
    size_t i2 = cuckoo_alt_index(i1, fp, mask);

    if (cuckoo_bucket_insert(&c->buckets[i1], fp))
        return true;
    if (cuckoo_bucket_insert(&c->buckets[i2], fp))
        return true;

    /* Cuckoo displacement */
    /* Simple LCG for random choices */
    static uint32_t rng_state = 42;
    size_t idx = (rng_state & 1) ? i1 : i2;
    rng_state = rng_state * 1103515245 + 12345;

    for (int kick = 0; kick < CUCKOO_MAX_KICKS; kick++) {
        /* Pick random slot in bucket */
        int slot = rng_state % CUCKOO_BUCKET_SIZE;
        rng_state = rng_state * 1103515245 + 12345;

        /* Swap */
        uint8_t evicted = c->buckets[idx].fps[slot];
        c->buckets[idx].fps[slot] = fp;
        fp = evicted;

        idx = cuckoo_alt_index(idx, fp, mask);
        if (cuckoo_bucket_insert(&c->buckets[idx], fp))
            return true;
    }
    return false; /* table full */
}

void cuckoo_build(cuckoo_t *c, char **words, size_t n) {
    size_t mask = c->num_buckets - 1;

    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(words[i]);
        uint8_t fp = cuckoo_fingerprint(words[i], len);
        size_t i1 = cuckoo_index1(words[i], len, mask);

        if (!cuckoo_insert_fp(c, fp, i1)) {
            /* Rebuild with larger table */
            size_t new_nb = c->num_buckets * 2;
            cuckoo_bucket_t *old = c->buckets;
            size_t old_nb = c->num_buckets;

            c->num_buckets = new_nb;
            c->buckets = (cuckoo_bucket_t *)calloc(new_nb, sizeof(cuckoo_bucket_t));
            c->mem_bytes = new_nb * sizeof(cuckoo_bucket_t);
            mask = new_nb - 1;

            /* Re-insert all from old table */
            for (size_t b = 0; b < old_nb; b++) {
                for (int s = 0; s < CUCKOO_BUCKET_SIZE; s++) {
                    if (old[b].fps[s] != 0) {
                        cuckoo_insert_fp(c, old[b].fps[s], b & mask);
                    }
                }
            }
            free(old);

            /* Re-try current word */
            fp = cuckoo_fingerprint(words[i], len);
            i1 = cuckoo_index1(words[i], len, mask);
            cuckoo_insert_fp(c, fp, i1);
        }
    }
}

bool cuckoo_lookup(const cuckoo_t *c, const char *word, size_t len) {
    size_t mask = c->num_buckets - 1;
    uint8_t fp = cuckoo_fingerprint(word, len);
    size_t i1 = cuckoo_index1(word, len, mask);
    size_t i2 = cuckoo_alt_index(i1, fp, mask);

    return cuckoo_bucket_contains(&c->buckets[i1], fp) ||
           cuckoo_bucket_contains(&c->buckets[i2], fp);
}

size_t cuckoo_memory(const cuckoo_t *c) {
    return c->mem_bytes;
}

void cuckoo_free(cuckoo_t *c) {
    free(c->buckets);
    c->buckets = NULL;
}
