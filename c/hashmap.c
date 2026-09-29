/*
 * hashmap.c - Open addressing hash table with FNV-1a hash.
 */

#include <stdlib.h>
#include <string.h>
#include "structures.h"

void hashmap_init(hashmap_t *hm, size_t expected) {
    /* Load factor ~0.5 for open addressing */
    hm->capacity = 1;
    while (hm->capacity < expected * 2)
        hm->capacity <<= 1;
    hm->count = 0;
    hm->mem_bytes = 0;

    size_t tbl_size = hm->capacity * sizeof(hm_entry_t);
    hm->table = (hm_entry_t *)calloc(hm->capacity, sizeof(hm_entry_t));
    hm->mem_bytes += tbl_size;
}

static void hashmap_insert(hashmap_t *hm, const char *word, size_t len) {
    uint64_t h = fnv1a_hash(word, len);
    size_t idx = h & (hm->capacity - 1);

    while (hm->table[idx].occupied) {
        if (hm->table[idx].key_len == len &&
            memcmp(hm->table[idx].key, word, len) == 0)
            return; /* already present */
        idx = (idx + 1) & (hm->capacity - 1);
    }

    hm->table[idx].key = (char *)malloc(len + 1);
    memcpy(hm->table[idx].key, word, len);
    hm->table[idx].key[len] = '\0';
    hm->table[idx].key_len = len;
    hm->table[idx].occupied = true;
    hm->count++;
    hm->mem_bytes += len + 1;
}

void hashmap_build(hashmap_t *hm, char **words, size_t n) {
    for (size_t i = 0; i < n; i++) {
        hashmap_insert(hm, words[i], strlen(words[i]));
    }
}

bool hashmap_lookup(const hashmap_t *hm, const char *word, size_t len) {
    uint64_t h = fnv1a_hash(word, len);
    size_t idx = h & (hm->capacity - 1);

    while (hm->table[idx].occupied) {
        if (hm->table[idx].key_len == len &&
            memcmp(hm->table[idx].key, word, len) == 0)
            return true;
        idx = (idx + 1) & (hm->capacity - 1);
    }
    return false;
}

size_t hashmap_memory(const hashmap_t *hm) {
    return hm->mem_bytes;
}

void hashmap_free(hashmap_t *hm) {
    for (size_t i = 0; i < hm->capacity; i++) {
        if (hm->table[i].occupied)
            free(hm->table[i].key);
    }
    free(hm->table);
    hm->table = NULL;
    hm->count = 0;
}
