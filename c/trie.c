/*
 * trie.c - Trie with linked-list children and arena allocation.
 *
 * Uses linked list of children per node instead of 256-way array
 * to reduce memory from ~2KB/node to ~12 bytes/node + ~12 bytes/child.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "structures.h"

#define NIL UINT32_MAX

void trie_init(trie_t *t, size_t max_nodes) {
    t->node_cap = max_nodes;
    t->node_used = 0;
    t->node_arena = (trie_node_t *)malloc(max_nodes * sizeof(trie_node_t));

    /* Estimate children: roughly same as nodes */
    t->child_cap = max_nodes;
    t->child_used = 0;
    t->child_arena = (trie_child_t *)malloc(max_nodes * sizeof(trie_child_t));

    t->mem_bytes = 0;
    t->root = NIL;
}

static uint32_t trie_alloc_node(trie_t *t) {
    if (t->node_used >= t->node_cap) {
        size_t new_cap = t->node_cap * 2;
        t->node_arena = (trie_node_t *)realloc(t->node_arena, new_cap * sizeof(trie_node_t));
        t->node_cap = new_cap;
    }
    uint32_t idx = (uint32_t)t->node_used++;
    t->node_arena[idx].first_child = NIL;
    t->node_arena[idx].is_end = false;
    return idx;
}

static uint32_t trie_alloc_child(trie_t *t) {
    if (t->child_used >= t->child_cap) {
        size_t new_cap = t->child_cap * 2;
        t->child_arena = (trie_child_t *)realloc(t->child_arena, new_cap * sizeof(trie_child_t));
        t->child_cap = new_cap;
    }
    return (uint32_t)t->child_used++;
}

/* Find or create child of node_idx with character ch */
static uint32_t trie_get_child(trie_t *t, uint32_t node_idx, unsigned char ch) {
    uint32_t ci = t->node_arena[node_idx].first_child;
    while (ci != NIL) {
        if (t->child_arena[ci].ch == ch)
            return t->child_arena[ci].node_idx;
        ci = t->child_arena[ci].next_sibling;
    }
    /* Create new child */
    uint32_t new_node = trie_alloc_node(t);
    uint32_t new_ci = trie_alloc_child(t);
    t->child_arena[new_ci].ch = ch;
    t->child_arena[new_ci].node_idx = new_node;
    t->child_arena[new_ci].next_sibling = t->node_arena[node_idx].first_child;
    t->node_arena[node_idx].first_child = new_ci;
    return new_node;
}

void trie_build(trie_t *t, char **words, size_t n) {
    t->root = trie_alloc_node(t);
    for (size_t i = 0; i < n; i++) {
        uint32_t node = t->root;
        const unsigned char *w = (const unsigned char *)words[i];
        while (*w) {
            node = trie_get_child(t, node, *w);
            w++;
        }
        t->node_arena[node].is_end = true;
    }
    t->mem_bytes = t->node_used * sizeof(trie_node_t) +
                   t->child_used * sizeof(trie_child_t);
}

bool trie_lookup(const trie_t *t, const char *word, size_t len) {
    uint32_t node = t->root;
    const unsigned char *w = (const unsigned char *)word;
    for (size_t i = 0; i < len; i++) {
        uint32_t ci = t->node_arena[node].first_child;
        bool found = false;
        while (ci != NIL) {
            if (t->child_arena[ci].ch == w[i]) {
                node = t->child_arena[ci].node_idx;
                found = true;
                break;
            }
            ci = t->child_arena[ci].next_sibling;
        }
        if (!found) return false;
    }
    return t->node_arena[node].is_end;
}

size_t trie_memory(const trie_t *t) {
    return t->mem_bytes;
}

void trie_free(trie_t *t) {
    free(t->node_arena);
    free(t->child_arena);
    t->node_arena = NULL;
    t->child_arena = NULL;
    t->node_used = 0;
    t->child_used = 0;
    t->root = NIL;
}
