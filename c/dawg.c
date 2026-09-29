/*
 * dawg.c - DAWG: build trie then compress shared suffixes bottom-up.
 *
 * Strategy:
 *   1. Build a trie from sorted words (using node arena + child arena).
 *   2. Bottom-up DFS: compute signature for each node, dedup nodes
 *      with identical signatures by remapping children.
 */

#include <stdlib.h>
#include <string.h>
#include "structures.h"

void dawg_init(dawg_t *d, size_t max_nodes, size_t max_children) {
    d->nodes_cap = max_nodes;
    d->nodes_used = 0;
    d->nodes = (dawg_node_t *)calloc(max_nodes, sizeof(dawg_node_t));

    d->child_arena_cap = max_children;
    d->child_arena_used = 0;
    d->child_arena = (dawg_child_t *)calloc(max_children, sizeof(dawg_child_t));

    d->remap = NULL;
    d->final_node_count = 0;
    d->mem_bytes = 0;
    d->root = 0;
}

static uint32_t dawg_alloc_node(dawg_t *d) {
    if (d->nodes_used >= d->nodes_cap) {
        size_t new_cap = d->nodes_cap * 2;
        d->nodes = (dawg_node_t *)realloc(d->nodes, new_cap * sizeof(dawg_node_t));
        memset(d->nodes + d->nodes_cap, 0, (new_cap - d->nodes_cap) * sizeof(dawg_node_t));
        d->nodes_cap = new_cap;
    }
    uint32_t idx = (uint32_t)d->nodes_used++;
    d->nodes[idx].children = NULL;
    d->nodes[idx].is_end = false;
    d->nodes[idx].sig = 0;
    return idx;
}

static dawg_child_t *dawg_alloc_child(dawg_t *d) {
    if (d->child_arena_used >= d->child_arena_cap) {
        size_t new_cap = d->child_arena_cap * 2;
        d->child_arena = (dawg_child_t *)realloc(d->child_arena, new_cap * sizeof(dawg_child_t));
        memset(d->child_arena + d->child_arena_cap, 0,
               (new_cap - d->child_arena_cap) * sizeof(dawg_child_t));
        d->child_arena_cap = new_cap;
    }
    return &d->child_arena[d->child_arena_used++];
}

/* Find or create child for character ch under node_idx */
static uint32_t dawg_get_child(dawg_t *d, uint32_t node_idx, unsigned char ch) {
    dawg_child_t *c = d->nodes[node_idx].children;
    while (c) {
        if (c->ch == ch) return c->node_idx;
        c = c->next;
    }
    /* Create new child */
    uint32_t new_node = dawg_alloc_node(d);
    dawg_child_t *new_child = dawg_alloc_child(d);
    new_child->ch = ch;
    new_child->node_idx = new_node;
    new_child->next = d->nodes[node_idx].children;
    d->nodes[node_idx].children = new_child;
    return new_node;
}

/* ---- Signature-based deduplication ---- */

/*
 * Hash table for dedup: maps signature -> canonical node index.
 * Simple open-addressing table.
 */
typedef struct {
    uint64_t sig;
    uint32_t node_idx;
    bool     occupied;
} sig_entry_t;

static sig_entry_t *sig_table = NULL;
static size_t sig_table_cap = 0;

static void sig_table_init(size_t cap) {
    sig_table_cap = cap;
    sig_table = (sig_entry_t *)calloc(cap, sizeof(sig_entry_t));
}

static void sig_table_free(void) {
    free(sig_table);
    sig_table = NULL;
}

/* Returns UINT32_MAX if not found */
static uint32_t sig_table_find(uint64_t sig) {
    size_t idx = sig & (sig_table_cap - 1);
    while (sig_table[idx].occupied) {
        if (sig_table[idx].sig == sig)
            return sig_table[idx].node_idx;
        idx = (idx + 1) & (sig_table_cap - 1);
    }
    return UINT32_MAX;
}

static void sig_table_insert(uint64_t sig, uint32_t node_idx) {
    size_t idx = sig & (sig_table_cap - 1);
    while (sig_table[idx].occupied) {
        idx = (idx + 1) & (sig_table_cap - 1);
    }
    sig_table[idx].sig = sig;
    sig_table[idx].node_idx = node_idx;
    sig_table[idx].occupied = true;
}

/*
 * Compute signature for a node bottom-up.
 * Signature = hash of (is_end, sorted list of (ch, child_signature)).
 * Then remap to canonical node if signature matches.
 */
static void dawg_compute_sig(dawg_t *d, uint32_t node_idx) {
    dawg_node_t *node = &d->nodes[node_idx];

    /* First, recursively compute sigs for children */
    for (dawg_child_t *c = node->children; c; c = c->next) {
        dawg_compute_sig(d, c->node_idx);
        /* Remap child to canonical */
        c->node_idx = d->remap[c->node_idx];
    }

    /* Build signature from is_end + children (ch, child_sig) */
    uint64_t sig = node->is_end ? 1ULL : 0ULL;
    sig = sig * 31 + 17;

    /* Collect children into temp array and sort by ch for deterministic sig */
    unsigned char chs[256];
    uint64_t child_sigs[256];
    int nc = 0;
    for (dawg_child_t *c = node->children; c; c = c->next) {
        chs[nc] = c->ch;
        child_sigs[nc] = d->nodes[c->node_idx].sig;
        nc++;
    }
    /* Simple insertion sort by ch */
    for (int i = 1; i < nc; i++) {
        unsigned char kc = chs[i];
        uint64_t ks = child_sigs[i];
        int j = i - 1;
        while (j >= 0 && chs[j] > kc) {
            chs[j+1] = chs[j];
            child_sigs[j+1] = child_sigs[j];
            j--;
        }
        chs[j+1] = kc;
        child_sigs[j+1] = ks;
    }
    for (int i = 0; i < nc; i++) {
        sig = sig * 131 + chs[i];
        sig = sig * 131 + child_sigs[i];
    }

    node->sig = sig;

    /* Check if we already have a canonical node with this signature */
    uint32_t canonical = sig_table_find(sig);
    if (canonical != UINT32_MAX) {
        /* Verify actual equality (handle hash collisions) */
        dawg_node_t *cn = &d->nodes[canonical];
        bool equal = (cn->is_end == node->is_end);
        if (equal) {
            /* Check children match */
            dawg_child_t *a = cn->children;
            dawg_child_t *b = node->children;
            while (a && b) {
                if (a->ch != b->ch || a->node_idx != b->node_idx) {
                    equal = false;
                    break;
                }
                a = a->next;
                b = b->next;
            }
            if (a || b) equal = false;
        }
        if (equal) {
            d->remap[node_idx] = canonical;
            return;
        }
    }

    /* This node is canonical */
    sig_table_insert(sig, node_idx);
    d->remap[node_idx] = node_idx;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

void dawg_build(dawg_t *d, char **words, size_t n) {
    /* Sort words for trie construction */
    char **sorted = (char **)malloc(n * sizeof(char *));
    memcpy(sorted, words, n * sizeof(char *));
    qsort(sorted, n, sizeof(char *), cmp_str);

    /* Step 1: Build trie */
    d->root = dawg_alloc_node(d);
    for (size_t i = 0; i < n; i++) {
        uint32_t node = d->root;
        const unsigned char *w = (const unsigned char *)sorted[i];
        while (*w) {
            node = dawg_get_child(d, node, *w);
            w++;
        }
        d->nodes[node].is_end = true;
    }
    free(sorted);

    /* Step 2: Sort children lists by ch for consistent ordering */
    for (size_t i = 0; i < d->nodes_used; i++) {
        /* Simple insertion sort of linked list by ch */
        dawg_child_t *sorted_head = NULL;
        dawg_child_t *c = d->nodes[i].children;
        while (c) {
            dawg_child_t *next = c->next;
            if (!sorted_head || c->ch < sorted_head->ch) {
                c->next = sorted_head;
                sorted_head = c;
            } else {
                dawg_child_t *p = sorted_head;
                while (p->next && p->next->ch < c->ch)
                    p = p->next;
                c->next = p->next;
                p->next = c;
            }
            c = next;
        }
        d->nodes[i].children = sorted_head;
    }

    /* Step 3: Bottom-up deduplication */
    d->remap = (uint32_t *)malloc(d->nodes_used * sizeof(uint32_t));
    for (size_t i = 0; i < d->nodes_used; i++)
        d->remap[i] = (uint32_t)i;

    /* Sig table size: power of 2 >= 2 * nodes_used */
    size_t stcap = 1;
    while (stcap < d->nodes_used * 2) stcap <<= 1;
    sig_table_init(stcap);

    dawg_compute_sig(d, d->root);
    d->root = d->remap[d->root];

    /* Count unique nodes */
    d->final_node_count = 0;
    for (size_t i = 0; i < d->nodes_used; i++) {
        if (d->remap[i] == (uint32_t)i)
            d->final_node_count++;
    }

    sig_table_free();

    d->mem_bytes = d->final_node_count * (sizeof(dawg_node_t) + 2 * sizeof(dawg_child_t));
}

bool dawg_lookup(const dawg_t *d, const char *word, size_t len) {
    uint32_t node_idx = d->root;
    const unsigned char *w = (const unsigned char *)word;

    for (size_t i = 0; i < len; i++) {
        dawg_child_t *c = d->nodes[node_idx].children;
        bool found = false;
        while (c) {
            if (c->ch == w[i]) {
                node_idx = c->node_idx;
                found = true;
                break;
            }
            c = c->next;
        }
        if (!found) return false;
    }
    return d->nodes[node_idx].is_end;
}

size_t dawg_memory(const dawg_t *d) {
    return d->mem_bytes;
}

void dawg_free(dawg_t *d) {
    free(d->nodes);
    free(d->child_arena);
    free(d->remap);
    d->nodes = NULL;
    d->child_arena = NULL;
    d->remap = NULL;
}
