/*
 * bloom.c - Bloom filter with double hashing (FNV-1a + djb2).
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "structures.h"

void bloom_init(bloom_t *b, size_t n, double fp_rate) {
    /* m = -n * ln(p) / (ln2)^2 */
    double ln2 = log(2.0);
    size_t m = (size_t)(-(double)n * log(fp_rate) / (ln2 * ln2));
    if (m < 64) m = 64;

    /* k = (m/n) * ln2 */
    int k = (int)((double)m / (double)n * ln2);
    if (k < 1) k = 1;

    b->size_bits = m;
    b->num_hashes = k;

    size_t num_bytes = (m + 7) / 8;
    b->bits = (uint8_t *)calloc(num_bytes, 1);
    b->mem_bytes = num_bytes;
}

static inline void bloom_set_bit(bloom_t *b, size_t pos) {
    b->bits[pos / 8] |= (1u << (pos % 8));
}

static inline bool bloom_get_bit(const bloom_t *b, size_t pos) {
    return (b->bits[pos / 8] & (1u << (pos % 8))) != 0;
}

void bloom_build(bloom_t *b, char **words, size_t n) {
    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(words[i]);
        uint64_t h1 = fnv1a_hash(words[i], len);
        uint64_t h2 = djb2_hash(words[i], len);

        for (int j = 0; j < b->num_hashes; j++) {
            size_t pos = (h1 + (uint64_t)j * h2) % b->size_bits;
            bloom_set_bit(b, pos);
        }
    }
}

bool bloom_lookup(const bloom_t *b, const char *word, size_t len) {
    uint64_t h1 = fnv1a_hash(word, len);
    uint64_t h2 = djb2_hash(word, len);

    for (int j = 0; j < b->num_hashes; j++) {
        size_t pos = (h1 + (uint64_t)j * h2) % b->size_bits;
        if (!bloom_get_bit(b, pos))
            return false;
    }
    return true;
}

size_t bloom_memory(const bloom_t *b) {
    return b->mem_bytes;
}

void bloom_free(bloom_t *b) {
    free(b->bits);
    b->bits = NULL;
}
