/*
 * tst.c - Ternary Search Tree with arena allocation.
 * Input is shuffled with Fisher-Yates (seed 42) for balance.
 */

#include <stdlib.h>
#include <string.h>
#include "structures.h"

void tst_init(tst_t *t, size_t max_nodes) {
    t->arena_cap = max_nodes;
    t->arena_used = 0;
    t->arena = (tst_node_t *)calloc(max_nodes, sizeof(tst_node_t));
    t->mem_bytes = max_nodes * sizeof(tst_node_t);
    t->root = NULL;
}

static tst_node_t *tst_alloc_node(tst_t *t, unsigned char ch) {
    if (t->arena_used >= t->arena_cap) {
        size_t new_cap = t->arena_cap * 2;
        t->arena = (tst_node_t *)realloc(t->arena, new_cap * sizeof(tst_node_t));
        memset(t->arena + t->arena_cap, 0, (new_cap - t->arena_cap) * sizeof(tst_node_t));
        t->mem_bytes += (new_cap - t->arena_cap) * sizeof(tst_node_t);
        t->arena_cap = new_cap;
    }
    tst_node_t *node = &t->arena[t->arena_used++];
    node->ch = ch;
    node->is_end = false;
    node->left = node->mid = node->right = NULL;
    return node;
}

static tst_node_t *tst_insert(tst_t *t, tst_node_t *node,
                                const unsigned char *word, size_t idx, size_t len) {
    unsigned char ch = word[idx];
    if (!node) {
        node = tst_alloc_node(t, ch);
    }

    if (ch < node->ch) {
        node->left = tst_insert(t, node->left, word, idx, len);
    } else if (ch > node->ch) {
        node->right = tst_insert(t, node->right, word, idx, len);
    } else {
        if (idx + 1 < len) {
            node->mid = tst_insert(t, node->mid, word, idx + 1, len);
        } else {
            node->is_end = true;
        }
    }
    return node;
}

/* Fisher-Yates shuffle with seed 42 using a simple LCG */
static void shuffle_words(char **words, size_t n) {
    /* Simple LCG: same as glibc's parameters */
    uint64_t state = 42;
    for (size_t i = n - 1; i > 0; i--) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        size_t j = (size_t)((state >> 33) % (i + 1));
        char *tmp = words[i];
        words[i] = words[j];
        words[j] = tmp;
    }
}

void tst_build(tst_t *t, char **words, size_t n) {
    /* Make a copy of pointers to shuffle */
    char **shuffled = (char **)malloc(n * sizeof(char *));
    memcpy(shuffled, words, n * sizeof(char *));
    shuffle_words(shuffled, n);

    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(shuffled[i]);
        if (len > 0) {
            t->root = tst_insert(t, t->root, (const unsigned char *)shuffled[i], 0, len);
        }
    }
    free(shuffled);

    /* Adjust mem_bytes to only count used nodes */
    t->mem_bytes = t->arena_used * sizeof(tst_node_t);
}

bool tst_lookup(const tst_t *t, const char *word, size_t len) {
    if (len == 0) return false;
    const unsigned char *w = (const unsigned char *)word;
    const tst_node_t *node = t->root;
    size_t idx = 0;

    while (node) {
        if (w[idx] < node->ch) {
            node = node->left;
        } else if (w[idx] > node->ch) {
            node = node->right;
        } else {
            if (idx + 1 == len)
                return node->is_end;
            node = node->mid;
            idx++;
        }
    }
    return false;
}

size_t tst_memory(const tst_t *t) {
    return t->mem_bytes;
}

void tst_free(tst_t *t) {
    free(t->arena);
    t->arena = NULL;
    t->arena_used = 0;
    t->root = NULL;
}
