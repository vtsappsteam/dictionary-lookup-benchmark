/*
 * structures.h - Declarations for 7 dictionary lookup data structures.
 *
 * Authors: Nikola Vukotic, Slavimir Stosovic
 * Date: Jun 2026
 */

#ifndef STRUCTURES_H
#define STRUCTURES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Maximum word length in bytes */
#define MAX_WORD_LEN 100

/* ============================================================
 * Timing helpers (macOS + Linux)
 * ============================================================ */

#ifdef __APPLE__
#include <mach/mach_time.h>
static inline double get_time_sec(void) {
    static mach_timebase_info_data_t tb;
    if (tb.denom == 0) mach_timebase_info(&tb);
    uint64_t t = mach_absolute_time();
    return (double)t * tb.numer / tb.denom / 1e9;
}
#else
#include <time.h>
static inline double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}
#endif

/* ============================================================
 * Memory tracking
 * ============================================================ */

extern size_t g_mem_allocated;

static inline void *tracked_malloc(size_t size) {
    void *p = malloc(size);
    if (p) g_mem_allocated += size;
    return p;
}

static inline void *tracked_calloc(size_t count, size_t size) {
    void *p = calloc(count, size);
    if (p) g_mem_allocated += count * size;
    return p;
}

/* ============================================================
 * Hash functions
 * ============================================================ */

static inline uint64_t fnv1a_hash(const char *data, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static inline uint64_t djb2_hash(const char *data, size_t len) {
    uint64_t h = 5381;
    for (size_t i = 0; i < len; i++) {
        h = ((h << 5) + h) + (uint8_t)data[i];
    }
    return h;
}

/* ============================================================
 * 1. HashMap (open addressing, FNV-1a)
 * ============================================================ */

typedef struct {
    char  *key;
    size_t key_len;
    bool   occupied;
} hm_entry_t;

typedef struct {
    hm_entry_t *table;
    size_t capacity;
    size_t count;
    size_t mem_bytes;
} hashmap_t;

void   hashmap_init(hashmap_t *hm, size_t expected);
void   hashmap_build(hashmap_t *hm, char **words, size_t n);
bool   hashmap_lookup(const hashmap_t *hm, const char *word, size_t len);
size_t hashmap_memory(const hashmap_t *hm);
void   hashmap_free(hashmap_t *hm);

/* ============================================================
 * 2. Sorted Array + Binary Search
 * ============================================================ */

typedef struct {
    char  **data;     /* array of pointers to words */
    size_t  count;
    size_t  mem_bytes;
} sorted_array_t;

void   sorted_array_init(sorted_array_t *sa);
void   sorted_array_build(sorted_array_t *sa, char **words, size_t n);
bool   sorted_array_lookup(const sorted_array_t *sa, const char *word);
size_t sorted_array_memory(const sorted_array_t *sa);
void   sorted_array_free(sorted_array_t *sa);

/* ============================================================
 * 3. Trie (Prefix Tree): linked-list children, arena allocated
 * ============================================================ */

typedef struct trie_child {
    unsigned char ch;
    uint32_t node_idx;       /* index into trie node arena */
    uint32_t next_sibling;   /* index into child arena, UINT32_MAX = end */
} trie_child_t;

typedef struct trie_node {
    uint32_t first_child;    /* index into child arena, UINT32_MAX = none */
    bool is_end;
} trie_node_t;

typedef struct {
    trie_node_t *node_arena;
    size_t node_used;
    size_t node_cap;
    trie_child_t *child_arena;
    size_t child_used;
    size_t child_cap;
    uint32_t root;
    size_t mem_bytes;
} trie_t;

void   trie_init(trie_t *t, size_t max_nodes);
void   trie_build(trie_t *t, char **words, size_t n);
bool   trie_lookup(const trie_t *t, const char *word, size_t len);
size_t trie_memory(const trie_t *t);
void   trie_free(trie_t *t);

/* ============================================================
 * 4. Ternary Search Tree (TST): arena allocated
 * ============================================================ */

typedef struct tst_node {
    unsigned char ch;
    bool is_end;
    struct tst_node *left, *mid, *right;
} tst_node_t;

typedef struct {
    tst_node_t *arena;
    size_t arena_used;
    size_t arena_cap;
    tst_node_t *root;
    size_t mem_bytes;
} tst_t;

void   tst_init(tst_t *t, size_t max_nodes);
void   tst_build(tst_t *t, char **words, size_t n);
bool   tst_lookup(const tst_t *t, const char *word, size_t len);
size_t tst_memory(const tst_t *t);
void   tst_free(tst_t *t);

/* ============================================================
 * 5. DAWG: trie plus bottom-up suffix sharing
 * ============================================================ */

/* DAWG node uses linked list for children to save memory after merging */
typedef struct dawg_child {
    unsigned char ch;
    uint32_t node_idx;
    struct dawg_child *next;
} dawg_child_t;

typedef struct {
    dawg_child_t *children; /* linked list */
    bool is_end;
    uint64_t sig;           /* signature for dedup */
} dawg_node_t;

typedef struct {
    dawg_node_t *nodes;
    size_t nodes_used;
    size_t nodes_cap;
    dawg_child_t *child_arena;
    size_t child_arena_used;
    size_t child_arena_cap;
    uint32_t root;
    /* remap table for dedup */
    uint32_t *remap;
    size_t final_node_count;
    size_t mem_bytes;
} dawg_t;

void   dawg_init(dawg_t *d, size_t max_nodes, size_t max_children);
void   dawg_build(dawg_t *d, char **words, size_t n);
bool   dawg_lookup(const dawg_t *d, const char *word, size_t len);
size_t dawg_memory(const dawg_t *d);
void   dawg_free(dawg_t *d);

/* ============================================================
 * 6. Bloom Filter
 * ============================================================ */

typedef struct {
    uint8_t *bits;
    size_t  size_bits;   /* m */
    int     num_hashes;  /* k */
    size_t  mem_bytes;
} bloom_t;

void   bloom_init(bloom_t *b, size_t n, double fp_rate);
void   bloom_build(bloom_t *b, char **words, size_t n);
bool   bloom_lookup(const bloom_t *b, const char *word, size_t len);
size_t bloom_memory(const bloom_t *b);
void   bloom_free(bloom_t *b);

/* ============================================================
 * 7. Cuckoo Filter: 4-way buckets, 8-bit fingerprints
 * ============================================================ */

#define CUCKOO_BUCKET_SIZE 4
#define CUCKOO_MAX_KICKS 500
#define CUCKOO_FP_BITS 8

typedef struct {
    uint8_t fps[CUCKOO_BUCKET_SIZE];
} cuckoo_bucket_t;

typedef struct {
    cuckoo_bucket_t *buckets;
    size_t num_buckets;
    size_t mem_bytes;
} cuckoo_t;

void   cuckoo_init(cuckoo_t *c, size_t n);
void   cuckoo_build(cuckoo_t *c, char **words, size_t n);
bool   cuckoo_lookup(const cuckoo_t *c, const char *word, size_t len);
size_t cuckoo_memory(const cuckoo_t *c);
void   cuckoo_free(cuckoo_t *c);

/* ============================================================
 * 7b. Cuckoo Filter, 16-bit fingerprints (ICAI revision)
 *     Second operating point so that Bloom and cuckoo can be
 *     compared at a matched false positive rate.
 * ============================================================ */

typedef struct {
    uint16_t fps[CUCKOO_BUCKET_SIZE];
} cuckoo16_bucket_t;

typedef struct {
    cuckoo16_bucket_t *buckets;
    size_t num_buckets;
    size_t mem_bytes;
} cuckoo16_t;

void   cuckoo16_init(cuckoo16_t *c, size_t n);
void   cuckoo16_build(cuckoo16_t *c, char **words, size_t n);
bool   cuckoo16_lookup(const cuckoo16_t *c, const char *word, size_t len);
size_t cuckoo16_memory(const cuckoo16_t *c);
void   cuckoo16_free(cuckoo16_t *c);

#endif /* STRUCTURES_H */
